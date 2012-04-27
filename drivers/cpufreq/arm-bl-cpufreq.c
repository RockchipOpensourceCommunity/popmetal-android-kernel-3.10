/*
 * arm-bl-cpufreq.c: Simple cpufreq backend for the ARM big.LITTLE switcher
 * Copyright (C) 2012  Linaro Limited
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#define MODULE_NAME "arm-bl-cpufreq"
#define pr_fmt(fmt) MODULE_NAME ": " fmt

#include <linux/bug.h>
#include <linux/cache.h>
#include <linux/cpufreq.h>
#include <linux/cpumask.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/printk.h>
#include <linux/string.h>

#include <asm/bL_switcher.h>


/* Dummy frequencies representing the big and little clusters: */
#define FREQ_BIG	1000000
#define FREQ_LITTLE	 100000

/*  Cluster numbers */
#define CLUSTER_BIG	0
#define CLUSTER_LITTLE	1

/*
 * Switch latency advertised to cpufreq.  This value is bogus and will
 * need to be properly calibrated when running on real hardware.
 */
#define BL_CPUFREQ_FAKE_LATENCY 1

static struct cpufreq_frequency_table __read_mostly bl_freqs[] = {
	{ CLUSTER_BIG,		FREQ_BIG		},
	{ CLUSTER_LITTLE,	FREQ_LITTLE		},
	{ 0,			CPUFREQ_TABLE_END	},
};

/* Cached current cluster for each CPU to save on IPIs */
static DEFINE_PER_CPU(unsigned int, cpu_cur_cluster);


/* Miscellaneous helpers */

static unsigned int entry_to_freq(
	struct cpufreq_frequency_table const *entry)
{
	return entry->frequency;
}

static unsigned int entry_to_cluster(
	struct cpufreq_frequency_table const *entry)
{
	return entry->index;
}

static struct cpufreq_frequency_table const *find_entry_by_cluster(int cluster)
{
	unsigned int i;

	for(i = 0; entry_to_freq(&bl_freqs[i]) != CPUFREQ_TABLE_END; i++)
		if(entry_to_cluster(&bl_freqs[i]) == cluster)
			return &bl_freqs[i];

	WARN(1, pr_fmt("%s(): invalid cluster number %d, assuming 0\n"),
		__func__, cluster);
	return &bl_freqs[0];
}

static unsigned int cluster_to_freq(int cluster)
{
	return entry_to_freq(find_entry_by_cluster(cluster));
}

/*
 * Functions to get the current status.
 *
 * Beware that the cluster for another CPU may change unexpectedly.
 */

static unsigned int get_local_cluster(void)
{
	unsigned int mpidr;
	asm ("mrc\tp15, 0, %0, c0, c0, 5" : "=r" (mpidr));
	return MPIDR_AFFINITY_LEVEL(mpidr, 1);
}

static void __get_current_cluster(void *_data)
{
	unsigned int *_cluster = _data;
	*_cluster = get_local_cluster();
}

static int get_current_cluster(unsigned int cpu)
{
	unsigned int cluster = 0;
	smp_call_function_single(cpu, __get_current_cluster, &cluster, 1);
	return cluster;
}

static int get_current_cached_cluster(unsigned int cpu)
{
	return per_cpu(cpu_cur_cluster, cpu);
}

static unsigned int get_current_freq(unsigned int cpu)
{
	return cluster_to_freq(get_current_cluster(cpu));
}

/*
 * Switch to the requested cluster.
 */
static void switch_to_entry(unsigned int cpu,
			    struct cpufreq_frequency_table const *target)
{
	int old_cluster, new_cluster;
	struct cpufreq_freqs freqs;

	old_cluster = get_current_cached_cluster(cpu);
	new_cluster = entry_to_cluster(target);

	pr_debug("Switching to cluster %d on CPU %d\n", new_cluster, cpu);

	if(new_cluster == old_cluster)
		return;

	freqs.cpu = cpu;
	freqs.old = cluster_to_freq(old_cluster);
	freqs.new = entry_to_freq(target);

	cpufreq_notify_transition(&freqs, CPUFREQ_PRECHANGE);
	bL_switch_request(cpu, new_cluster);
	per_cpu(cpu_cur_cluster, cpu) = new_cluster;
	cpufreq_notify_transition(&freqs, CPUFREQ_POSTCHANGE);
}


/* Cpufreq methods and module code */

static int bl_cpufreq_init(struct cpufreq_policy *policy)
{
	unsigned int cluster, cpu = policy->cpu;
	int err;

	/*
	 * Set CPU and policy min and max frequencies based on bl_freqs:
	 */
	err = cpufreq_frequency_table_cpuinfo(policy, bl_freqs);
	if (err)
		goto error;

	cluster = get_current_cluster(cpu);
	per_cpu(cpu_cur_cluster, cpu) = cluster;

	/*
	 * Ideally, transition_latency should be calibrated here.
	 */
	policy->cpuinfo.transition_latency = BL_CPUFREQ_FAKE_LATENCY;
	policy->cur = cluster_to_freq(cluster);
	policy->shared_type = CPUFREQ_SHARED_TYPE_NONE;

	pr_info("cpufreq initialised successfully\n");
	return 0;

error:
	pr_warning("cpufreq initialisation failed (%d)\n", err);
	return err;
}

static int bl_cpufreq_verify(struct cpufreq_policy *policy)
{
	return cpufreq_frequency_table_verify(policy, bl_freqs);
}

static int bl_cpufreq_target(struct cpufreq_policy *policy,
			     unsigned int target_freq,
			     unsigned int relation)
{
	int err;
	int index;

	err = cpufreq_frequency_table_target(policy, bl_freqs, target_freq,
					     relation, &index);
	if(err)
		return err;

	switch_to_entry(policy->cpu, &bl_freqs[index]);
	return 0;
}

static unsigned int bl_cpufreq_get(unsigned int cpu)
{
	return get_current_freq(cpu);
}

static struct cpufreq_driver __read_mostly bl_cpufreq_driver = {
	.owner = THIS_MODULE,
	.name = MODULE_NAME,

	.init = bl_cpufreq_init,
	.verify = bl_cpufreq_verify,
	.target = bl_cpufreq_target,
	.get = bl_cpufreq_get,
	/* what else? */
};

static int __init bl_cpufreq_module_init(void)
{
	int err;

	err = cpufreq_register_driver(&bl_cpufreq_driver);
	if(err)
		pr_info("cpufreq backend driver registration failed (%d)\n",
			err);
	else
		pr_info("cpufreq backend driver registered.\n");

	return err;
}
module_init(bl_cpufreq_module_init);

static void __exit bl_cpufreq_module_exit(void)
{
	cpufreq_unregister_driver(&bl_cpufreq_driver);
	pr_info("cpufreq backend driver unloaded.\n");
}
module_exit(bl_cpufreq_module_exit);


MODULE_AUTHOR("Dave Martin");
MODULE_DESCRIPTION("Simple cpufreq interface for the ARM big.LITTLE switcher");
MODULE_LICENSE("GPL");
