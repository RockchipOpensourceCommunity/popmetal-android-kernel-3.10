/*
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 *
 * Copyright (C) 2012 ARM Limited
 *
 * Author: Will Deacon <will.deacon@arm.com>
 */
#define pr_fmt(fmt) "CPU PMU: " fmt

#include <linux/bitmap.h>
#include <linux/cpu_pm.h>
#include <linux/export.h>
#include <linux/kernel.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/spinlock.h>

#include <asm/cputype.h>
#include <asm/irq_regs.h>
#include <asm/pmu.h>

static DEFINE_PER_CPU(struct perf_event * [ARMPMU_MAX_HWEVENTS], hw_events);
static DEFINE_PER_CPU(unsigned long [BITS_TO_LONGS(ARMPMU_MAX_HWEVENTS)], used_mask);
static DEFINE_PER_CPU(struct pmu_hw_events, cpu_hw_events);
static DEFINE_PER_CPU(struct arm_pmu *, armcpu_pmu);
static LIST_HEAD(cpupmu_list);
static DEFINE_PER_CPU(struct cpupmu_regs, cpu_pmu_regs);

/*
 * Despite the names, these two functions are CPU-specific and are used
 * by the OProfile/perf code.
 */
const char *perf_pmu_name(void)
{
	struct arm_pmu *cpu_pmu = per_cpu(armcpu_pmu, 0);
	if (!cpu_pmu)
		return NULL;

	return cpu_pmu->pmu.name;
}
EXPORT_SYMBOL_GPL(perf_pmu_name);

int perf_num_counters(void)
{
	struct arm_pmu *cpu_pmu = per_cpu(armcpu_pmu, 0);
	int max_events = 0;

	if (cpu_pmu != NULL)
		max_events = cpu_pmu->num_events;

	return max_events;
}
EXPORT_SYMBOL_GPL(perf_num_counters);

static struct pmu_hw_events *cpu_pmu_get_cpu_events(void)
{
	return &__get_cpu_var(cpu_hw_events);
}

static void cpu_pmu_free_irq(struct arm_pmu *cpu_pmu)
{
	int i, irq, irqs;
	struct platform_device *pmu_device = cpu_pmu->plat_device;
	int cpu = -1;

	irqs = min(pmu_device->num_resources, num_possible_cpus());

	for (i = 0; i < irqs; ++i) {
		cpu = cpumask_next(cpu, &cpu_pmu->cpus);
		if (!cpumask_test_and_clear_cpu(cpu, &cpu_pmu->active_irqs))
			continue;
		irq = platform_get_irq(pmu_device, i);
		if (irq >= 0)
			free_irq(irq, cpu_pmu);
	}
}

static int cpu_pmu_request_irq(struct arm_pmu *cpu_pmu, irq_handler_t handler)
{
	int i, err, irq, irqs;
	struct platform_device *pmu_device = cpu_pmu->plat_device;
	int cpu = -1;

	if (!pmu_device)
		return -ENODEV;

	irqs = min(pmu_device->num_resources, num_possible_cpus());
	if (irqs < 1) {
		pr_err("no irqs for PMUs defined\n");
		return -ENODEV;
	}

	for (i = 0; i < irqs; ++i) {
		err = 0;
		cpu = cpumask_next(cpu, &cpu_pmu->cpus);
		irq = platform_get_irq(pmu_device, i);
		if (irq < 0)
			continue;

		/*
		 * If we have a single PMU interrupt that we can't shift,
		 * assume that we're running on a uniprocessor machine and
		 * continue. Otherwise, continue without this interrupt.
		 */
		if (irq_set_affinity(irq, cpumask_of(cpu)) && irqs > 1) {
			pr_warning("unable to set irq affinity (irq=%d, cpu=%u)\n",
				    irq, i);
			continue;
		}

		err = request_irq(irq, handler, IRQF_NOBALANCING, "arm-pmu",
				  cpu_pmu);
		if (err) {
			pr_err("unable to request IRQ%d for ARM PMU counters\n",
				irq);
			return err;
		}

		cpumask_set_cpu(cpu, &cpu_pmu->active_irqs);
	}

	return 0;
}

static void __devinit cpu_pmu_init(struct arm_pmu *cpu_pmu)
{
	int cpu;
	for_each_possible_cpu(cpu) {
		struct pmu_hw_events *events = &per_cpu(cpu_hw_events, cpu);
		events->events = per_cpu(hw_events, cpu);
		events->used_mask = per_cpu(used_mask, cpu);
		raw_spin_lock_init(&events->pmu_lock);
	}

	cpu_pmu->get_hw_events	= cpu_pmu_get_cpu_events;
	cpu_pmu->request_irq	= cpu_pmu_request_irq;
	cpu_pmu->free_irq	= cpu_pmu_free_irq;
}

/*
 * PMU hardware loses all context when a CPU goes offline.
 * When a CPU is hotplugged back in, since some hardware registers are
 * UNKNOWN at reset, the PMU must be explicitly reset to avoid reading
 * junk values out of them.
 */
static int __cpuinit cpu_pmu_notify(struct notifier_block *b,
				    unsigned long action, void *hcpu)
{
	int cpu = smp_processor_id();
	struct arm_pmu *cpu_pmu = per_cpu(armcpu_pmu, cpu);
	if ((action & ~CPU_TASKS_FROZEN) != CPU_STARTING)
		return NOTIFY_DONE;

	if (cpu_pmu && cpu_pmu->reset)
		cpu_pmu->reset(cpu_pmu);

	return NOTIFY_OK;
}

static int cpu_pmu_pm_notify(struct notifier_block *b,
				    unsigned long action, void *hcpu)
{
	int cpu = smp_processor_id();
	struct arm_pmu *cpu_pmu = per_cpu(armcpu_pmu, cpu);
	struct cpupmu_regs *pmuregs = &per_cpu(cpu_pmu_regs, cpu);

	if (action == CPU_PM_ENTER && cpu_pmu->save_regs) {
		cpu_pmu->save_regs(cpu_pmu, pmuregs);
		cpu_pmu->reset(cpu_pmu);
	} else if (action == CPU_PM_EXIT && cpu_pmu->restore_regs) {
		cpu_pmu->restore_regs(cpu_pmu, pmuregs);
	}

	return NOTIFY_OK;
}

static struct notifier_block __cpuinitdata cpu_pmu_hotplug_notifier = {
	.notifier_call = cpu_pmu_notify,
};

static struct notifier_block __cpuinitdata cpu_pmu_pm_notifier = {
	.notifier_call = cpu_pmu_pm_notify,
};

int cpu_pmu_register(struct cpu_pmu_info *cpupmu_info)
{
	if (!strlen(cpupmu_info->compatible) ||	!cpupmu_info->init
			|| (!cpupmu_info->impl && !cpupmu_info->part))
		return -EINVAL;
	list_add(&cpupmu_info->entry, &cpupmu_list);
	return 0;
}

struct cpu_pmu_info *cpupmu_match_compatible(const char *compatible)
{
	struct cpu_pmu_info *temp, *pmu_info = ERR_PTR(-ENODEV);
	list_for_each_entry(temp, &cpupmu_list, entry) {
		if (!strcmp(compatible, temp->compatible)) {
			pmu_info = temp;
			break;
		}
	}
	return pmu_info;
}

struct cpu_pmu_info *cpupmu_match_impl_part(unsigned long impl,
						unsigned long part)
{
	struct cpu_pmu_info *temp, *pmu_info = ERR_PTR(-ENODEV);
	list_for_each_entry(temp, &cpupmu_list, entry) {
		if (impl == temp->impl && part == temp->part) {
			pmu_info = temp;
			break;
		}
	}
	return pmu_info;
}

/*
 * PMU platform driver and devicetree bindings.
 */
static struct of_device_id __devinitdata cpu_pmu_of_device_ids[] = {
	{.compatible = "arm,cortex-a15-pmu"},
	{.compatible = "arm,cortex-a9-pmu"},
	{.compatible = "arm,cortex-a8-pmu"},
	{.compatible = "arm,cortex-a7-pmu"},
	{.compatible = "arm,cortex-a5-pmu"},
	{.compatible = "arm,arm11mpcore-pmu"},
	{.compatible = "arm,arm1176-pmu"},
	{.compatible = "arm,arm1136-pmu"},
	{},
};

static struct platform_device_id __devinitdata cpu_pmu_plat_device_ids[] = {
	{.name = "arm-pmu"},
	{},
};

/*
 * CPU PMU identification and probing.
 */
static int __devinit probe_current_pmu(struct arm_pmu *pmu)
{
	int cpu = get_cpu();
	unsigned long cpuid = read_cpuid_id();
	unsigned long implementor = (cpuid & 0xFF000000) >> 24;
	unsigned long part_number = (cpuid & 0xFFF0);
	struct cpu_pmu_info *pmu_info = NULL;
	int ret = -ENODEV;

	pr_info("probing PMU on CPU %d\n", cpu);

	/* Intel CPUs [xscale]. */
	if (0x69 == implementor)
		part_number = (cpuid >> 13) & 0x7;

	pmu_info = cpupmu_match_impl_part(implementor, part_number);
	if (!IS_ERR_OR_NULL(pmu_info))
		ret = pmu_info->init(pmu);
	put_cpu();
	return ret;
}

static int __devinit cpu_pmu_device_probe(struct platform_device *pdev)
{
	const struct of_device_id *of_id;
	struct cpu_pmu_info *pmu_info = NULL;
	struct device_node *node = pdev->dev.of_node, *ncluster;
	struct arm_pmu *cpupmu;
	int ret = 0;
	int cluster = -1;

	cpupmu = kzalloc(sizeof(struct arm_pmu), GFP_KERNEL);
	if (IS_ERR_OR_NULL(cpupmu)) {
		pr_info("failed to allocate PMU device");
		return -ENOMEM;
	}

	if (node && (of_id = of_match_node(cpu_pmu_of_device_ids, pdev->dev.of_node))) {
		pmu_info = cpupmu_match_compatible(of_id->compatible);
		if (!IS_ERR_OR_NULL(pmu_info)) {
			int sibling;
			cpumask_t sibling_mask;
			smp_call_func_t init = (smp_call_func_t)pmu_info->init;
			ncluster = of_parse_phandle(node, "cluster", 0);
			if (ncluster) {
				int len;
				const u32 *hwid;
				hwid = of_get_property(ncluster, "reg", &len);
				if (hwid && len == 4)
					cluster = be32_to_cpup(hwid);
			}
			/* set sibling mask to all cpu mask if socket is not specified */
			if (cluster == -1 ||
				cluster_to_logical_mask(cluster, &sibling_mask))
				cpumask_copy(&sibling_mask, cpu_possible_mask);
			smp_call_function_any(&sibling_mask, init, cpupmu, 1);
			for_each_cpu(sibling, &sibling_mask)
				per_cpu(armcpu_pmu, sibling) = cpupmu;
			cpumask_copy(&cpupmu->cpus, &sibling_mask);
			/* Ensure the PMU has sane values out of reset */
			on_each_cpu_mask(&sibling_mask, cpupmu->reset, cpupmu, 1);
		}
	} else {
		ret = probe_current_pmu(cpupmu);
	}

	if (ret) {
		pr_info("failed to register PMU devices!");
		kfree(cpupmu);
		return ret;
	}

	cpupmu->plat_device = pdev;
	cpu_pmu_init(cpupmu);
	if (!cluster) {
		register_cpu_notifier(&cpu_pmu_hotplug_notifier);
		cpu_pm_register_notifier(&cpu_pmu_pm_notifier);
	}
	armpmu_register(cpupmu, cpupmu->name, PERF_TYPE_RAW);

	return 0;
}

static struct platform_driver cpu_pmu_driver = {
	.driver		= {
		.name	= "arm-pmu",
		.pm	= &armpmu_dev_pm_ops,
		.of_match_table = cpu_pmu_of_device_ids,
	},
	.probe		= cpu_pmu_device_probe,
	.id_table	= cpu_pmu_plat_device_ids,
};

static int __init register_pmu_driver(void)
{
	return platform_driver_register(&cpu_pmu_driver);
}
device_initcall(register_pmu_driver);
