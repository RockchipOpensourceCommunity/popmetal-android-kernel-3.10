/*
 * ARM big.LITTLE Platforms CPUFreq support
 *
 * Copyright (C) 2012 ARM Ltd.
 * Author: Sudeep KarkadaNagesha <sudeep.karkadanagesha@arm.com>
 *
 * Copyright (C) 2012 Linaro.
 * Viresh Kumar <viresh.kumar@linaro.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed "as is" WITHOUT ANY WARRANTY of any
 * kind, whether express or implied; without even the implied warranty
 * of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/clk.h>
#include <linux/cpufreq.h>
#include <linux/cpumask.h>
#include <linux/export.h>
#include <linux/of_platform.h>
#include <linux/slab.h>
#include <linux/types.h>
#include <asm/bL_switcher.h>
#include <asm/topology.h>
#include "arm_big_little.h"

#ifdef CONFIG_BL_SWITCHER
static bool bL_switching_enabled;
#define is_bL_switching_enabled()		bL_switching_enabled
#define set_switching_enabled(x) 		(bL_switching_enabled = (x))
#else
#define is_bL_switching_enabled()		false
#define set_switching_enabled(x) 		do { } while (0)
#endif

#define A15_CLUSTER	0
#define A7_CLUSTER	1
#define MAX_CLUSTERS	2

#define ACTUAL_FREQ(cluster, freq)	((cluster == A15_CLUSTER) ? freq >> 1 : freq)
#define VIRT_FREQ(cluster, freq)	((cluster == A15_CLUSTER) ? freq << 1 : freq)

static struct cpufreq_arm_bL_ops *arm_bL_ops;
static struct clk *clk[MAX_CLUSTERS];
static struct cpufreq_frequency_table *freq_table[MAX_CLUSTERS + 1];
static int freq_table_cnt[MAX_CLUSTERS];
static atomic_t cluster_usage[MAX_CLUSTERS + 1] = {ATOMIC_INIT(0), ATOMIC_INIT(0)};

static unsigned int clk_big_min;	/* (Big) clock frequencies */
static unsigned int clk_little_max;	/* Maximum clock frequency (Little) */

static DEFINE_PER_CPU(unsigned int, physical_cluster);
static DEFINE_PER_CPU(unsigned int, cpu_last_req_freq);

/*
 * Functions to get the current status.
 *
 * Beware that the cluster for another CPU may change unexpectedly.
 */
static int cpu_to_cluster(int cpu)
{
	return is_bL_switching_enabled() ? MAX_CLUSTERS:
		topology_physical_package_id(cpu);
}

static unsigned int find_cluster_maxfreq(int cpu, int cluster, unsigned int new)
{
	int j;

	for_each_online_cpu(j) {
		unsigned int temp = per_cpu(cpu_last_req_freq, j);

		if (cpu == j)
			continue;
		if (cluster == per_cpu(physical_cluster, j) &&
			new < temp)
			new = temp;
	}

	pr_debug("%s: cluster: %d, max freq: %d\n", __func__, cluster, new);

	return new;
}

static unsigned int clk_get_cpu_rate(unsigned int cpu)
{
	u32 cur_cluster = per_cpu(physical_cluster, cpu);
	u32 rate = clk_get_rate(clk[cur_cluster]) / 1000;

	/* For switcher we use virtual A15 clock rates */
	if (is_bL_switching_enabled())
		rate = VIRT_FREQ(cur_cluster, rate);

	pr_debug("%s: cpu: %d, cluster: %d, freq: %u\n", __func__, cpu,
			cur_cluster, rate);

	return rate;
}

static unsigned int bL_cpufreq_get_rate(unsigned int cpu)
{
	pr_debug("%s: freq: %d\n", __func__, per_cpu(cpu_last_req_freq, cpu));

	return per_cpu(cpu_last_req_freq, cpu);
}

static unsigned int
bL_cpufreq_set_rate(u32 cpu, u32 old_cluster, u32 new_cluster, u32 rate)
{
	u32 new_rate;
	int ret;

	if (is_bL_switching_enabled()) {
		new_rate = find_cluster_maxfreq(cpu, new_cluster, rate);
		new_rate = ACTUAL_FREQ(new_cluster, new_rate);
	} else {
		new_rate = rate;
	}

	pr_debug("%s: cpu: %d, old cluster: %d, new cluster: %d, freq: %d\n",
			__func__, cpu, old_cluster, new_cluster, new_rate);

	ret = clk_set_rate(clk[new_cluster], new_rate * 1000);
	if (ret) {
		pr_err("clk_set_rate failed: %d, new cluster: %d\n", ret,
				new_cluster);
		return ret;
	}

	/* Recalc freq for old cluster when switching clusters */
	if (old_cluster != new_cluster) {
		new_rate = find_cluster_maxfreq(cpu, old_cluster, 0);
		new_rate = ACTUAL_FREQ(old_cluster, new_rate);

		/* Set freq of old cluster if there are cpus left on it */
		if (new_rate) {
			pr_debug("%s: Updating rate of old cluster: %d, to freq: %d\n",
					__func__, old_cluster, new_rate);

			if (clk_set_rate(clk[old_cluster], new_rate * 1000))
				pr_err("%s: clk_set_rate failed: %d, old cluster: %d\n",
						__func__, ret, old_cluster);
		}
	}

	per_cpu(cpu_last_req_freq, cpu) = rate;
	return 0;
}

/* Validate policy frequency range */
static int bL_cpufreq_verify_policy(struct cpufreq_policy *policy)
{
	u32 cur_cluster = cpu_to_cluster(policy->cpu);

	/* This call takes care of it all using freq_table */
	return cpufreq_frequency_table_verify(policy, freq_table[cur_cluster]);
}

/* Set clock frequency */
static int bL_cpufreq_set_target(struct cpufreq_policy *policy,
		unsigned int target_freq, unsigned int relation)
{
	struct cpufreq_freqs freqs;
	u32 cpu = policy->cpu, freq_tab_idx, cur_cluster, new_cluster,
	    actual_cluster;
	int ret = 0;

	/* ASSUMPTION: The cpu can't be hotplugged in this function */
	cur_cluster = cpu_to_cluster(cpu);
	new_cluster = actual_cluster = per_cpu(physical_cluster, cpu);

	freqs.cpu = cpu;
	freqs.old = bL_cpufreq_get_rate(cpu);

	/* Determine valid target frequency using freq_table */
	cpufreq_frequency_table_target(policy, freq_table[cur_cluster],
			target_freq, relation, &freq_tab_idx);
	freqs.new = freq_table[cur_cluster][freq_tab_idx].frequency;

	pr_debug("%s: cpu: %d, cluster: %d, oldfreq: %d, target freq: %d, new freq: %d\n",
			__func__, cpu, cur_cluster, freqs.old, target_freq,
			freqs.new);

	if (freqs.old == freqs.new)
		return 0;

	if (is_bL_switching_enabled()) {
		if ((actual_cluster == A15_CLUSTER) &&
				(freqs.new < clk_big_min)) {
			new_cluster = A7_CLUSTER;
		} else if ((actual_cluster == A7_CLUSTER) &&
				(freqs.new > clk_little_max)) {
			new_cluster = A15_CLUSTER;
		}
	}

	for_each_cpu(freqs.cpu, policy->cpus)
		cpufreq_notify_transition(&freqs, CPUFREQ_PRECHANGE);

	ret = bL_cpufreq_set_rate(cpu, actual_cluster, new_cluster, freqs.new);
	if (ret)
		return ret;

	if (new_cluster != actual_cluster) {
		pr_debug("%s: old cluster: %d, new cluster: %d\n", __func__,
				actual_cluster, new_cluster);

		bL_switch_request(cpu, new_cluster);
		per_cpu(physical_cluster, cpu) = new_cluster;
	}

	policy->cur = freqs.new;

	for_each_cpu(freqs.cpu, policy->cpus)
		cpufreq_notify_transition(&freqs, CPUFREQ_POSTCHANGE);

	return ret;
}

/* get the minimum frequency in the cpufreq_frequency_table */
static inline u32 get_table_min(struct cpufreq_frequency_table *table)
{
	int i;
	uint32_t min_freq = ~0;
	for (i = 0; (table[i].frequency != CPUFREQ_TABLE_END); i++)
		if (table[i].frequency < min_freq)
			min_freq = table[i].frequency;
	return min_freq;
}

/* get the maximum frequency in the cpufreq_frequency_table */
static inline u32 get_table_max(struct cpufreq_frequency_table *table)
{
	int i;
	uint32_t max_freq = 0;
	for (i = 0; (table[i].frequency != CPUFREQ_TABLE_END); i++)
		if (table[i].frequency > max_freq)
			max_freq = table[i].frequency;
	return max_freq;
}

/* translate the integer array into cpufreq_frequency_table entries */
struct cpufreq_frequency_table *
arm_bL_copy_table_from_array(unsigned int *table, int count)
{
	int i;

	struct cpufreq_frequency_table *freq_table;

	pr_debug("%s: table: %p, count: %d\n", __func__, table, count);

	freq_table = kmalloc(sizeof(*freq_table) * (count + 1), GFP_KERNEL);
	if (!freq_table)
		return NULL;

	for (i = 0; i < count; i++) {
		pr_debug("%s: index: %d, freq: %d\n", __func__, i, table[i]);
		freq_table[i].index = i;
		freq_table[i].frequency = table[i]; /* in kHZ */
	}

	freq_table[i].index = count;
	freq_table[i].frequency = CPUFREQ_TABLE_END;

	return freq_table;
}
EXPORT_SYMBOL_GPL(arm_bL_copy_table_from_array);

void arm_bL_free_freq_table(u32 cluster)
{
	pr_debug("%s: free freq table\n", __func__);

	kfree(freq_table[cluster]);
}
EXPORT_SYMBOL_GPL(arm_bL_free_freq_table);

static int merge_cluster_tables(void)
{
	int i, j, k = 0, count = 1;
	struct cpufreq_frequency_table *table;

	for (i = 0; i < MAX_CLUSTERS; i++)
		count += freq_table_cnt[i];

	table = kzalloc(sizeof(*table) * count, GFP_KERNEL);
	if (!table)
		return -ENOMEM;

	freq_table[MAX_CLUSTERS] = table;

	/* Add in reverse order to get freqs in increasing order */
	for (i = MAX_CLUSTERS - 1; i >= 0; i--) {
		for (j = 0; j < freq_table_cnt[i]; j++) {
			table[k].index = k;
			table[k].frequency = VIRT_FREQ(i,
					freq_table[i][j].frequency);
			pr_debug("%s: index: %d, freq: %d\n", __func__, k,
					table[k].frequency);
			k++;
		}
	}

	table[k].index = k;
	table[k].frequency = CPUFREQ_TABLE_END;

	pr_debug("%s: End, table: %p, count: %d\n", __func__, table, k);

	return 0;
}

static void _put_cluster_clk_and_freq_table(u32 cluster)
{
	if (!atomic_dec_return(&cluster_usage[cluster])) {
		clk_put(clk[cluster]);
		clk[cluster] = NULL;
		arm_bL_ops->put_freq_tbl(cluster);
		freq_table[cluster] = NULL;
		pr_debug("%s: cluster: %d\n", __func__, cluster);
	}
}

static void put_cluster_clk_and_freq_table(u32 cluster)
{
	int i;

	if (cluster < MAX_CLUSTERS)
		return _put_cluster_clk_and_freq_table(cluster);

	if (atomic_dec_return(&cluster_usage[MAX_CLUSTERS]))
		return;

	for (i = 0; i < MAX_CLUSTERS; i++)
		return _put_cluster_clk_and_freq_table(i);
}

static int _get_cluster_clk_and_freq_table(u32 cluster)
{
	char name[9] = "cluster";
	int count;

	if (atomic_inc_return(&cluster_usage[cluster]) != 1)
		return 0;

	freq_table[cluster] = arm_bL_ops->get_freq_tbl(cluster, &count);
	if (!freq_table[cluster])
		goto atomic_dec;

	freq_table_cnt[cluster] = count;

	name[7] = cluster + '0';
	clk[cluster] = clk_get(NULL, name);
	if (!IS_ERR_OR_NULL(clk[cluster])) {
		pr_debug("%s: clk: %p & freq table: %p, cluster: %d\n",
				__func__, clk[cluster], freq_table[cluster],
				cluster);
		return 0;
	}

	arm_bL_ops->put_freq_tbl(cluster);

atomic_dec:
	atomic_dec(&cluster_usage[cluster]);
	pr_err("%s: Failed to get data for cluster: %d\n", __func__, cluster);
	return -ENODATA;
}

static int get_cluster_clk_and_freq_table(u32 cluster)
{
	int i, ret;

	if (cluster < MAX_CLUSTERS)
		return _get_cluster_clk_and_freq_table(cluster);

	if (atomic_inc_return(&cluster_usage[MAX_CLUSTERS]) != 1)
		return 0;

	/*
	 * Get data for all clusters and fill virtual cluster with a merge of
	 * both
	 */
	for (i = 0; i < MAX_CLUSTERS; i++) {
		ret = _get_cluster_clk_and_freq_table(i);
		if (ret)
			goto put_clusters;
	}

	ret = merge_cluster_tables();
	if (ret)
		goto put_clusters;

	/* Assuming 2 cluster, set clk_big_min and clk_little_max */
	clk_big_min = VIRT_FREQ(0, get_table_min(freq_table[0]));
	clk_little_max = get_table_max(freq_table[1]);

	pr_debug("%s: cluster: %d, clk_big_min: %d, clk_little_max: %d\n",
			__func__, cluster, clk_big_min, clk_little_max);

	return 0;

put_clusters:
	while (i)
		_put_cluster_clk_and_freq_table(--i);

	atomic_dec(&cluster_usage[MAX_CLUSTERS]);

	return ret;
}

/* Per-CPU initialization */
static int bL_cpufreq_init(struct cpufreq_policy *policy)
{
	u32 cur_cluster = cpu_to_cluster(policy->cpu);
	int result;

	result = get_cluster_clk_and_freq_table(cur_cluster);
	if (result)
		return result;

	result = cpufreq_frequency_table_cpuinfo(policy,
			freq_table[cur_cluster]);
	if (result) {
		pr_err("CPU %d, cluster: %d invalid freq table\n", policy->cpu,
				cur_cluster);
		put_cluster_clk_and_freq_table(cur_cluster);
		return result;
	}

	cpufreq_frequency_table_get_attr(freq_table[cur_cluster], policy->cpu);

	if (cur_cluster < MAX_CLUSTERS) {
		cpumask_copy(policy->cpus, topology_core_cpumask(policy->cpu));
		cpumask_copy(policy->related_cpus, policy->cpus);

		per_cpu(physical_cluster, policy->cpu) = cur_cluster;
	} else {
		/* Assumption: during init, we are always running on A15 */
		per_cpu(physical_cluster, policy->cpu) = A15_CLUSTER;
	}

	policy->cpuinfo.transition_latency = 1000000;	/* 1 ms assumed */
	policy->cur = clk_get_cpu_rate(policy->cpu);
	per_cpu(cpu_last_req_freq, policy->cpu) = policy->cur;

	pr_info("%s: Initialized, cpu: %d, cluster %d\n", __func__,
			policy->cpu, cur_cluster);

	return 0;
}

/* Export freq_table to sysfs */
static struct freq_attr *bL_cpufreq_attr[] = {
	&cpufreq_freq_attr_scaling_available_freqs,
	NULL,
};

static struct cpufreq_driver bL_cpufreq_driver = {
	.name	= "arm-big-little",
	.flags	= CPUFREQ_STICKY,
	.verify	= bL_cpufreq_verify_policy,
	.target	= bL_cpufreq_set_target,
	.get	= bL_cpufreq_get_rate,
	.init	= bL_cpufreq_init,
	.attr	= bL_cpufreq_attr,
};

static int bL_cpufreq_switcher_notifier(struct notifier_block *nfb,
					unsigned long action, void *_arg)
{
	pr_debug("%s: action: %ld\n", __func__, action);

	switch (action) {
	case BL_NOTIFY_PRE_ENABLE:
	case BL_NOTIFY_PRE_DISABLE:
		cpufreq_unregister_driver(&bL_cpufreq_driver);
		break;

	case BL_NOTIFY_POST_ENABLE:
		set_switching_enabled(true);
		cpufreq_register_driver(&bL_cpufreq_driver);
		break;

	case BL_NOTIFY_POST_DISABLE:
		set_switching_enabled(false);
		cpufreq_register_driver(&bL_cpufreq_driver);
		break;

	default:
		return NOTIFY_DONE;
	}

	return NOTIFY_OK;
}

static struct notifier_block bL_switcher_notifier = {
	.notifier_call = bL_cpufreq_switcher_notifier,
};

int bL_cpufreq_register(struct cpufreq_arm_bL_ops *ops)
{
	int ret;

	if (arm_bL_ops) {
		pr_debug("%s: Already registered: %s, exiting\n", __func__,
				arm_bL_ops->name);
		return -EBUSY;
	}

	if (!ops || !strlen(ops->name) || !ops->get_freq_tbl) {
		pr_err("%s: Invalid arm_bL_ops, exiting\n", __func__);
		return -ENODEV;
	}

	arm_bL_ops = ops;

	ret = bL_switcher_get_enabled();
	set_switching_enabled(ret);

	ret = cpufreq_register_driver(&bL_cpufreq_driver);
	if (ret) {
		pr_info("%s: Failed registering platform driver: %s, err: %d\n",
				__func__, ops->name, ret);
		arm_bL_ops = NULL;
	} else {
		ret = bL_switcher_register_notifier(&bL_switcher_notifier);
		if (ret) {
			cpufreq_unregister_driver(&bL_cpufreq_driver);
			arm_bL_ops = NULL;
		} else {
			pr_info("%s: Registered platform driver: %s\n",
					__func__, ops->name);
		}
	}

	bL_switcher_put_enabled();
	return ret;
}
EXPORT_SYMBOL_GPL(bL_cpufreq_register);

void bL_cpufreq_unregister(struct cpufreq_arm_bL_ops *ops)
{
	if (arm_bL_ops != ops) {
		pr_info("%s: Registered with: %s, can't unregister, exiting\n",
				__func__, arm_bL_ops->name);
	}

	bL_switcher_get_enabled();
	bL_switcher_unregister_notifier(&bL_switcher_notifier);
	cpufreq_unregister_driver(&bL_cpufreq_driver);
	bL_switcher_put_enabled();
	pr_info("%s: Un-registered platform driver: %s\n", __func__,
			arm_bL_ops->name);

	/* For saving table get/put on every cpu in/out */
	if (is_bL_switching_enabled()) {
		put_cluster_clk_and_freq_table(MAX_CLUSTERS);
	} else {
		int i;

		for (i = 0; i < MAX_CLUSTERS; i++)
			return put_cluster_clk_and_freq_table(i);
	}

	arm_bL_ops = NULL;
}
EXPORT_SYMBOL_GPL(bL_cpufreq_unregister);
