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
#include <linux/cpumask.h>
#include <linux/cpu_pm.h>
#include <linux/export.h>
#include <linux/kernel.h>
#include <linux/list.h>
#include <linux/of.h>
#include <linux/percpu.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/spinlock.h>

#include <asm/bL_switcher.h>
#include <asm/cputype.h>
#include <asm/irq_regs.h>
#include <asm/pmu.h>
#include <asm/smp_plat.h>
#include <asm/topology.h>

static LIST_HEAD(cpu_pmus_list);

#define cpu_for_each_pmu(pmu, cpu_pmu, cpu)				\
	for_each_pmu(pmu, &cpu_pmus_list)				\
		if (((cpu_pmu) = per_cpu_ptr((pmu)->cpu_pmus, cpu))->valid)

static struct arm_pmu *__cpu_find_any_pmu(unsigned int cpu)
{
	struct arm_pmu *pmu;
	struct arm_cpu_pmu *cpu_pmu;

	cpu_for_each_pmu(pmu, cpu_pmu, cpu)
		return pmu;

	return NULL;
}

/*
 * Despite the names, these two functions are CPU-specific and are used
 * by the OProfile/perf code.
 */
const char *perf_pmu_name(void)
{
	struct arm_pmu *pmu = __cpu_find_any_pmu(0);
	if (!pmu)
		return NULL;

	return pmu->name;
}
EXPORT_SYMBOL_GPL(perf_pmu_name);

int perf_num_counters(void)
{
	struct arm_pmu *pmu = __cpu_find_any_pmu(0);

	if (!pmu)
		return 0;

	return pmu->num_events;
}
EXPORT_SYMBOL_GPL(perf_num_counters);

/* Include the PMU-specific implementations. */
#include "perf_event_xscale.c"
#include "perf_event_v6.c"
#include "perf_event_v7.c"

static struct pmu_hw_events *cpu_pmu_get_cpu_events(struct arm_pmu *pmu)
{
	return &this_cpu_ptr(pmu->cpu_pmus)->cpu_hw_events;
}

static int find_logical_cpu(u32 mpidr)
{
	int cpu = bL_switcher_get_logical_index(mpidr);

	if (cpu != -EUNATCH)
		return cpu;

	return get_logical_index(mpidr);
}

static void cpu_pmu_free_irq(struct arm_pmu *pmu)
{
	int i;
	int cpu;
	struct arm_cpu_pmu *cpu_pmu;

	for_each_possible_cpu(i) {
		if (!(cpu_pmu = per_cpu_ptr(pmu->cpu_pmus, i)))
			continue;

		if (cpu_pmu->mpidr == -1)
			continue;

		cpu = find_logical_cpu(cpu_pmu->mpidr);
		if (cpu < 0)
			continue;

		if (!cpumask_test_and_clear_cpu(cpu, &pmu->active_irqs))
			continue;
		if (cpu_pmu->irq >= 0)
			free_irq(cpu_pmu->irq, pmu);
	}
}

static int cpu_pmu_request_irq(struct arm_pmu *pmu, irq_handler_t handler)
{
	int i, err, irq, irqs;
	int cpu;
	struct arm_cpu_pmu *cpu_pmu;

	irqs = 0;
	for_each_possible_cpu(i)
		if (per_cpu_ptr(pmu->cpu_pmus, i))
			++irqs;

	if (irqs < 1) {
		pr_err("no irqs for PMUs defined\n");
		return -ENODEV;
	}

	for_each_possible_cpu(i) {
		if (!(cpu_pmu = per_cpu_ptr(pmu->cpu_pmus, i)))
			continue;

		irq = cpu_pmu->irq;
		if (irq < 0)
			continue;

		cpu = find_logical_cpu(cpu_pmu->mpidr);
		if (cpu < 0 || cpu != i)
			continue;

		/*
		 * If we have a single PMU interrupt that we can't shift,
		 * assume that we're running on a uniprocessor machine and
		 * continue. Otherwise, continue without this interrupt.
		 */
		if (irq_set_affinity(irq, cpumask_of(cpu)) && irqs > 1) {
			pr_warning("unable to set irq affinity (irq=%d, cpu=%u)\n",
				    irq, cpu);
			continue;
		}

		pr_debug("%s: requesting IRQ %d for CPU%d\n",
			 pmu->name, irq, cpu);

		err = request_irq(irq, handler, IRQF_NOBALANCING, "arm-pmu",
				  pmu);
		if (err) {
			pr_err("unable to request IRQ%d for ARM PMU counters\n",
				irq);
			return err;
		}

		cpumask_set_cpu(cpu, &pmu->active_irqs);
	}

	return 0;
}

static void cpu_pmu_init(struct arm_pmu *pmu)
{
	int cpu;
	for_each_cpu_mask(cpu, pmu->valid_cpus) {
		struct arm_cpu_pmu *cpu_pmu = per_cpu_ptr(pmu->cpu_pmus, cpu);
		struct pmu_hw_events *events = &cpu_pmu->cpu_hw_events;

		events->events = cpu_pmu->hw_events;
		events->used_mask = cpu_pmu->used_mask;
		raw_spin_lock_init(&events->pmu_lock);

		if (pmu->cpu_init)
			pmu->cpu_init(pmu, cpu_pmu);

		cpu_pmu->valid = true;
	}

	pmu->get_hw_events	= cpu_pmu_get_cpu_events;
	pmu->request_irq	= cpu_pmu_request_irq;
	pmu->free_irq		= cpu_pmu_free_irq;

	/* Ensure the PMU has sane values out of reset. */
	if (pmu->reset)
		on_each_cpu_mask(&pmu->valid_cpus, pmu->reset, pmu, 1);
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
	struct arm_pmu *pmu;
	struct arm_cpu_pmu *cpu_pmu;
	int ret = NOTIFY_DONE;

	if ((action & ~CPU_TASKS_FROZEN) != CPU_STARTING)
		return NOTIFY_DONE;

	cpu_for_each_pmu(pmu, cpu_pmu, (unsigned int)hcpu)
		if (pmu->reset) {
			pmu->reset(pmu);
			ret = NOTIFY_OK;
		}

	return ret;
}

static int cpu_pmu_pm_notify(struct notifier_block *b,
				    unsigned long action, void *hcpu)
{
	int cpu = smp_processor_id();
	struct arm_pmu *pmu;
	struct arm_cpu_pmu *cpu_pmu;
	int ret = NOTIFY_DONE;

	cpu_for_each_pmu(pmu, cpu_pmu, cpu) {
		struct cpupmu_regs *pmuregs = &cpu_pmu->cpu_pmu_regs;

		if (action == CPU_PM_ENTER && pmu->save_regs)
			pmu->save_regs(pmu, pmuregs);
		else if (action == CPU_PM_EXIT && pmu->restore_regs)
			pmu->restore_regs(pmu, pmuregs);

		ret = NOTIFY_OK;
	}

	return ret;
}

static struct notifier_block __cpuinitdata cpu_pmu_hotplug_notifier = {
	.notifier_call = cpu_pmu_notify,
};

static struct notifier_block __cpuinitdata cpu_pmu_pm_notifier = {
	.notifier_call = cpu_pmu_pm_notify,
};

/*
 * PMU platform driver and devicetree bindings.
 */
static struct of_device_id cpu_pmu_of_device_ids[] = {
	{.compatible = "arm,cortex-a15-pmu",	.data = armv7_a15_pmu_init},
	{.compatible = "arm,cortex-a9-pmu",	.data = armv7_a9_pmu_init},
	{.compatible = "arm,cortex-a8-pmu",	.data = armv7_a8_pmu_init},
	{.compatible = "arm,cortex-a7-pmu",	.data = armv7_a7_pmu_init},
	{.compatible = "arm,cortex-a5-pmu",	.data = armv7_a5_pmu_init},
	{.compatible = "arm,arm11mpcore-pmu",	.data = armv6mpcore_pmu_init},
	{.compatible = "arm,arm1176-pmu",	.data = armv6pmu_init},
	{.compatible = "arm,arm1136-pmu",	.data = armv6pmu_init},
	{},
};

static struct platform_device_id cpu_pmu_plat_device_ids[] = {
	{.name = "arm-pmu"},
	{},
};

/*
 * CPU PMU identification and probing.
 */
static int probe_current_pmu(struct arm_pmu *pmu)
{
	int cpu = get_cpu();
	unsigned long implementor = read_cpuid_implementor();
	unsigned long part_number = read_cpuid_part_number();
	int ret = -ENODEV;

	pr_info("probing PMU on CPU %d\n", cpu);

	/* ARM Ltd CPUs. */
	if (implementor == ARM_CPU_IMP_ARM) {
		switch (part_number) {
		case ARM_CPU_PART_ARM1136:
		case ARM_CPU_PART_ARM1156:
		case ARM_CPU_PART_ARM1176:
			ret = armv6pmu_init(pmu);
			break;
		case ARM_CPU_PART_ARM11MPCORE:
			ret = armv6mpcore_pmu_init(pmu);
			break;
		case ARM_CPU_PART_CORTEX_A8:
			ret = armv7_a8_pmu_init(pmu);
			break;
		case ARM_CPU_PART_CORTEX_A9:
			ret = armv7_a9_pmu_init(pmu);
			break;
		case ARM_CPU_PART_CORTEX_A5:
			ret = armv7_a5_pmu_init(pmu);
			break;
		case ARM_CPU_PART_CORTEX_A15:
			ret = armv7_a15_pmu_init(pmu);
			break;
		case ARM_CPU_PART_CORTEX_A7:
			ret = armv7_a7_pmu_init(pmu);
			break;
		}
	/* Intel CPUs [xscale]. */
	} else if (implementor == ARM_CPU_IMP_INTEL) {
		switch (xscale_cpu_arch_version()) {
		case ARM_CPU_XSCALE_ARCH_V1:
			ret = xscale1pmu_init(pmu);
			break;
		case ARM_CPU_XSCALE_ARCH_V2:
			ret = xscale2pmu_init(pmu);
			break;
		}
	}

	/* assume PMU support all the CPUs in this case */
	cpumask_setall(&pmu->valid_cpus);

	put_cpu();
	return ret;
}

static void cpu_pmu_free(struct arm_pmu *pmu)
{
	if (!pmu)
		return;

	free_percpu(pmu->cpu_pmus);
	kfree(pmu);
}

/*
 * HACK: Find a b.L switcher partner for CPU cpu on the specified cluster
 * This information should be obtained from an interface provided by the
 * Switcher itself, if possible.
 */
#ifdef CONFIG_BL_SWITCHER
static int bL_get_partner(int cpu, int cluster)
{
	unsigned int i;


	for_each_possible_cpu(i) {
		if (cpu_topology[i].thread_id == cpu_topology[cpu].thread_id &&
		    cpu_topology[i].core_id == cpu_topology[cpu].core_id &&
		    cpu_topology[i].socket_id == cluster)
			return i;
	}

	return -1; /* no partner found */
}
#else
static int bL_get_partner(int __always_unused cpu, int __always_unused cluster)
{
	return -1;
}
#endif

static int find_irq(struct platform_device *pdev,
		    struct device_node *pmu_node,
		    struct device_node *cluster_node,
		    u32 mpidr)
{
	int irq = -1;
	u32 cluster;
	u32 core;
	struct device_node *cores_node;
	struct device_node *core_node = NULL;

	if (of_property_read_u32(cluster_node, "reg", &cluster) ||
	    cluster != MPIDR_AFFINITY_LEVEL(mpidr, 1))
		goto error;

	cores_node = of_get_child_by_name(cluster_node, "cores");
	if (!cores_node)
		goto error;

	for_each_child_of_node(cores_node, core_node)
		if (!of_property_read_u32(core_node, "reg", &core) &&
		    core == MPIDR_AFFINITY_LEVEL(mpidr, 0))
			break;

	if (!core_node)
		goto error;

	irq = platform_get_irq(pdev, core);

error:
	of_node_put(core_node);
	of_node_put(cores_node);
	return irq;
}

static int cpu_pmu_device_probe(struct platform_device *pdev)
{
	const struct of_device_id *of_id;
	struct device_node *node = pdev->dev.of_node;
	struct arm_pmu *pmu;
	struct arm_cpu_pmu __percpu *cpu_pmus;
	int ret = 0;

	pmu = kzalloc(sizeof(struct arm_pmu), GFP_KERNEL);
	if (!pmu)
		goto error_nomem;

	pmu->cpu_pmus = cpu_pmus = alloc_percpu(struct arm_cpu_pmu);
	if (!cpu_pmus)
		goto error_nomem;

	if (node && (of_id = of_match_node(cpu_pmu_of_device_ids, pdev->dev.of_node))) {
		smp_call_func_t init_fn = (smp_call_func_t)of_id->data;
		struct device_node *ncluster;
		int cluster = -1;
		cpumask_t sibling_mask;
		cpumask_t phys_sibling_mask;
		unsigned int i;

		ncluster = of_parse_phandle(node, "cluster", 0);
		if (ncluster) {
			int len;
			const u32 *hwid;
			hwid = of_get_property(ncluster, "reg", &len);
			if (hwid && len == 4)
				cluster = be32_to_cpup(hwid);
		}
		/* set sibling mask to all cpu mask if socket is not specified */
		/*
		 * In a switcher kernel, we affine all PMUs to CPUs and
		 * abstract the runtime presence/absence of PMUs at a lower
		 * level.
		 */
		if (cluster == -1 || IS_ENABLED(CONFIG_BL_SWITCHER) ||
			cluster_to_logical_mask(cluster, &sibling_mask))
			cpumask_copy(&sibling_mask, cpu_possible_mask);

		if (bL_switcher_get_enabled())
			/*
			 * The switcher initialises late now, so it should not
			 * have initialised yet:
			 */
			BUG();

		cpumask_copy(&phys_sibling_mask, cpu_possible_mask);

		/*
		 * HACK: Deduce how the switcher will modify the topology
		 * in order to fill in PMU<->CPU combinations which don't
		 * make sense when the switcher is disabled.  Ideally, this
		 * knowledge should come from the swithcer somehow.
		 */
		for_each_possible_cpu(i) {
			int cpu = i;

			per_cpu_ptr(cpu_pmus, i)->mpidr = -1;
			per_cpu_ptr(cpu_pmus, i)->irq = -1;

			if (cpu_topology[i].socket_id != cluster) {
				cpumask_clear_cpu(i, &phys_sibling_mask);
				cpu = bL_get_partner(i, cluster);
			}

			if (cpu == -1)
				cpumask_clear_cpu(i, &sibling_mask);
			else {
				int irq = find_irq(pdev, node, ncluster,
						   cpu_logical_map(cpu));
				per_cpu_ptr(cpu_pmus, i)->mpidr =
					cpu_logical_map(cpu);
				per_cpu_ptr(cpu_pmus, i)->irq = irq;
			}
		}

		/*
		 * This relies on an MP view of the system to choose the right
		 * CPU to run init_fn:
		 */
		smp_call_function_any(&phys_sibling_mask, init_fn, pmu, 1);

		bL_switcher_put_enabled();

		/* now set the valid_cpus after init */
		cpumask_copy(&pmu->valid_cpus, &sibling_mask);
	} else {
		ret = probe_current_pmu(pmu);
	}

	if (ret)
		goto error;

	pmu->plat_device = pdev;
	cpu_pmu_init(pmu);
	ret = armpmu_register(pmu, -1);

	if (ret)
		goto error;

	list_add(&pmu->class_pmus_list, &cpu_pmus_list);
	goto out;

error_nomem:
	pr_warn("out of memory\n");
	ret = -ENOMEM;
error:
	pr_warn("failed to register PMU device(s)!\n");
	cpu_pmu_free(pmu);
out:
	return ret;
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
	int err;

	err = register_cpu_notifier(&cpu_pmu_hotplug_notifier);
	if (err)
		return err;

	err = cpu_pm_register_notifier(&cpu_pmu_pm_notifier);
	if (err) {
		unregister_cpu_notifier(&cpu_pmu_hotplug_notifier);
		return err;
	}

	err = platform_driver_register(&cpu_pmu_driver);
	if (err) {
		cpu_pm_unregister_notifier(&cpu_pmu_pm_notifier);
		unregister_cpu_notifier(&cpu_pmu_hotplug_notifier);
	}

	return err;
}
device_initcall(register_pmu_driver);
