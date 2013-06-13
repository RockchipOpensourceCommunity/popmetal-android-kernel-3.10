/*
 * arm-bl-cpufreqtests.c: Unit tests on the simple cpufreq backend for the
 * ARM big.LITTLE switcher
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

#ifndef ARM_BL_CPUFREQ_DEFINE_TESTS
#include <linux/cpufreq.h>
#include <linux/module.h>
#include <linux/printk.h>
#include <linux/delay.h>
#include <linux/sched.h>

#include "arm-bl-cpufreq.h"

static short int test_config;

static int pre_init_tests(void);
static int post_init_tests(void);

#else /* ! ARM_BL_CPUFREQ_DEFINE_TESTS */

#ifdef CONFIG_ARM_BL_CPUFREQ_TEST

#ifdef pr_fmt
#undef pr_fmt
#endif
#define pr_fmt(fmt) __module_pr_fmt("[test] ", fmt)

#define SWITCH_DELAY 10
#define SWITCH_TRANSITION_DELAY 200
#define POST_INIT_TESTS_DELAY 100

static DECLARE_WAIT_QUEUE_HEAD(test_wq);
static int test_transition_count;
unsigned int test_transition_freq;

module_param(test_config, short, 1);
MODULE_PARM_DESC(test_config, "Make tests before registering cpufreq driver. (0 : no tests, 1 : tests and registering driver (default))");

static struct cpufreq_frequency_table const *get_other_entry(
				struct cpufreq_frequency_table const *entry)
{
	if (entry_to_cluster(entry) == CLUSTER_BIG)
		return find_entry_by_cluster(CLUSTER_LITTLE);
	else
		return find_entry_by_cluster(CLUSTER_BIG);
}

static int test_cpufreq_frequency_table(void)
{
	int nTest = 0, failCount = 0, testResult = 0;
	struct cpufreq_frequency_table const *entry;

	/* Get big and little cpufreq_frequency_table entries and check
	 * entry_to_freq() and entry_to_cluster() return corresponding
	 * frequencies and cluster id.
	 */
	entry = find_entry_by_cluster(CLUSTER_BIG);

	++nTest;
	if (entry_to_freq(entry) != FREQ_BIG) {
		testResult = 0;
		++failCount;
	} else
		testResult = 1;
	pr_info("name=pre-init/frequency_table/%d:entry_to_freq(big) result=%s\n",
					nTest, (testResult ? "PASS" : "FAIL"));

	++nTest;
	if (entry_to_cluster(entry) != CLUSTER_BIG) {
		testResult = 0;
		++failCount;
	} else
		testResult = 1;
	pr_info("name=pre-init/frequency_table/%d:entry_to_cluster(big) result=%s\n",
					nTest, (testResult ? "PASS" : "FAIL"));

	entry = find_entry_by_cluster(CLUSTER_LITTLE);

	++nTest;
	if (entry_to_freq(entry) != FREQ_LITTLE) {
		testResult = 0;
		++failCount;
	} else
		testResult = 1;
	pr_info("name=pre-init/frequency_table/%d:entry_to_freq(little) result=%s\n",
					nTest, (testResult ? "PASS" : "FAIL"));

	++nTest;
	if (entry_to_cluster(entry) != CLUSTER_LITTLE) {
		testResult = 0;
		++failCount;
	} else
		testResult = 1;
	pr_info("name=pre-init/frequency_table/%d:entry_to_cluster(little) result=%s\n",
					nTest, (testResult ? "PASS" : "FAIL"));

	pr_info("name=pre-init/frequency_table run=%d result=%s pass=%d fail=%d\n",
				nTest, (failCount == 0 ? "PASS" : "FAIL"),
				(nTest - failCount), failCount);
	if (failCount != 0)
		return -1;

	return 0;
}

static int test_cluster_to_freq(void)
{
	int nTest = 0, failCount = 0, testResult = 0;

	/* Check if test_cluster_to_freq() result is consistent, ie :
	 *	- CLUSTER_BIG => FREQ_BIG
	 *	- CLUSTER_LITTLE => FREQ_LITTLE
	 */
	++nTest;
	if (cluster_to_freq(CLUSTER_BIG) != FREQ_BIG) {
		testResult = 0;
		++failCount;
	} else
		testResult = 1;
	pr_info("name=pre-init/cluster_to_freq/%d:cluster_to_freq(big) result=%s\n",
					nTest, (testResult ? "PASS" : "FAIL"));

	++nTest;
	if (cluster_to_freq(CLUSTER_LITTLE) != FREQ_LITTLE) {
		testResult = 0;
		++failCount;
	} else
		testResult = 1;
	pr_info("name=pre-init/cluster_to_freq/%d:cluster_to_freq(little) result=%s\n",
					nTest, (testResult ? "PASS" : "FAIL"));

	pr_info("name=pre-init/cluster_to_freq run=%d result=%s pass=%d fail=%d\n",
				nTest, (failCount == 0 ? "PASS" : "FAIL"),
				(nTest - failCount), failCount);
	if (failCount != 0)
		return -1;

	return 0;
}

static int test_get_current_cluster(void)
{
	int nTest = 0, failCount = 0, testResult = 0;
	unsigned int cluster, cpu;

	/* Check if get_current_cluster() return a consistent value, ie
	 * CLUSTER_BIG or CLUSTER_LITTLE
	 */
	for_each_cpu(cpu, cpu_present_mask) {
		cluster = get_current_cluster(cpu);
		++nTest;
		if ((cluster != CLUSTER_BIG) && (cluster != CLUSTER_LITTLE)) {
			testResult = 0;
			++failCount;
		} else
			testResult = 1;
		pr_info("name=pre-init/get_current_cluster/%d:get_current_cluster(%u) result=%s\n",
				nTest, cpu, (testResult ? "PASS" : "FAIL"));
	}

	pr_info("name=pre-init/get_current_cluster run=%d result=%s pass=%d fail=%d\n",
				nTest, (failCount == 0 ? "PASS" : "FAIL"),
				(nTest - failCount), failCount);
	if (failCount != 0)
		return -1;

	return 0;
}

static int test_bl_cpufreq_get(void)
{
	int nTest = 0, failCount = 0, testResult = 0;
	unsigned int cpu;
	struct cpufreq_frequency_table const *other_entry = NULL;
	struct cpufreq_frequency_table const *origin_entry = NULL;
	struct cpufreq_policy *policy = NULL;

	/*
	 * Check bl_cpufreq_get() return value : for all cores value has to be
	 * the frequency of origin_entry
	 */
	for_each_cpu(cpu, cpu_present_mask) {
		policy = cpufreq_cpu_get(cpu);
		origin_entry = find_entry_by_cluster(get_current_cluster(cpu));
		other_entry = get_other_entry(origin_entry);

		++nTest;
		if (bl_cpufreq_get(cpu) != entry_to_freq(origin_entry)) {
			testResult = 0;
			++failCount;
		} else
			testResult = 1;
		pr_info("name=post-init/bl_cpufreq_get/%d:origin(%u) result=%s\n",
				nTest, cpu, (testResult ? "PASS" : "FAIL"));

		/*
		 * Switch to "other" cluster, ie cluster not used at module
		 * loading time
		 */
		cpufreq_driver_target(policy, entry_to_freq(other_entry),
							CPUFREQ_RELATION_H);

		++nTest;
		if (bl_cpufreq_get(cpu) != entry_to_freq(other_entry)) {
			testResult = 0;
			++failCount;
		} else
			testResult = 1;
		pr_info("name=post-init/bl_cpufreq_get/%d:other(%u) result=%s\n",
				nTest, cpu, (testResult ? "PASS" : "FAIL"));

		/*
		 * Switch back to "origin" cluster, ie cluster used at module
		 * loading time
		 */
		cpufreq_driver_target(policy, entry_to_freq(origin_entry),
							CPUFREQ_RELATION_H);
		cpufreq_cpu_put(policy);
	}

	pr_info("name=post-init/bl_cpufreq_get run=%d result=%s pass=%d fail=%d\n",
				nTest, (failCount == 0 ? "PASS" : "FAIL"),
				(nTest - failCount), failCount);
	if (failCount != 0)
		return -1;

	return 0;
}

static int test_get_current_freq(void)
{
	int nTest = 0, failCount = 0, testResult = 0;
	unsigned int cpu;
	struct cpufreq_frequency_table const *other_entry = NULL;
	struct cpufreq_frequency_table const *origin_entry = NULL;
	struct cpufreq_policy *policy = NULL;

	/*
	 * Check if get_current_freq() return a consistent value, ie
	 * FREQ_BIG while on big cluster and FREQ_LITTLE on little cluster
	 */
	for_each_cpu(cpu, cpu_present_mask) {
		policy = cpufreq_cpu_get(cpu);
		origin_entry = find_entry_by_cluster(get_current_cluster(cpu));
		other_entry = get_other_entry(origin_entry);

		++nTest;
		if (get_current_freq(cpu) != entry_to_freq(origin_entry)) {
			testResult = 0;
			++failCount;
		} else
			testResult = 1;
		pr_info("name=post-init/get_current_freq/%d:origin(%u) result=%s\n",
				nTest, cpu, (testResult ? "PASS" : "FAIL"));

		/*
		 * Switch to "other" cluster, ie cluster not used at module
		 * loading time
		 */
		cpufreq_driver_target(policy, entry_to_freq(other_entry),
							CPUFREQ_RELATION_H);

		++nTest;
		if (get_current_freq(cpu) != entry_to_freq(other_entry)) {
			testResult = 0;
			++failCount;
		} else
			testResult = 1;
		pr_info("name=post-init/get_current_freq/%d:other(%u) result=%s\n",
				nTest, cpu, (testResult ? "PASS" : "FAIL"));

		/*
		 * Switch back to "origin" cluster, ie cluster used at module
		 * loading time
		 */
		cpufreq_driver_target(policy, entry_to_freq(origin_entry),
							CPUFREQ_RELATION_H);
		cpufreq_cpu_put(policy);
	}

	pr_info("name=post-init/get_current_freq run=%d result=%s pass=%d fail=%d\n",
				nTest, (failCount == 0 ? "PASS" : "FAIL"),
				(nTest - failCount), failCount);
	if (failCount != 0)
		return -1;

	return 0;
}

static int test_get_current_cached_cluster(void)
{
	int nTest = 0, failCount = 0, testResult = 0;
	unsigned int cpu, cluster;
	struct cpufreq_frequency_table const *other_entry = NULL;
	struct cpufreq_frequency_table const *origin_entry = NULL;
	struct cpufreq_policy *policy = NULL;

	/*
	 * Check if get_current_cached_cluster() return a consistent value, ie
	 * CLUSTER_BIG while on big cluster and CLUSTER_LITTLE on little cluster
	 */
	for_each_cpu(cpu, cpu_present_mask) {
		policy = cpufreq_cpu_get(cpu);
		origin_entry = find_entry_by_cluster(get_current_cluster(cpu));
		other_entry = get_other_entry(origin_entry);

		++nTest;
		cluster = get_current_cached_cluster(cpu);
		if (cluster != entry_to_cluster(origin_entry)) {
			testResult = 0;
			++failCount;
		} else
			testResult = 1;
		pr_info("name=post-init/get_current_cached_cluster/%d:origin(%u) result=%s\n",
				nTest, cpu, (testResult ? "PASS" : "FAIL"));

		/*
		 * Switch to "other" cluster, ie cluster not used at module
		 * loading time
		 */
		cpufreq_driver_target(policy, entry_to_freq(other_entry),
							CPUFREQ_RELATION_H);

		++nTest;
		cluster = get_current_cached_cluster(cpu);
		if (cluster != entry_to_cluster(other_entry)) {
			testResult = 0;
			++failCount;
		} else
			testResult = 1;
		pr_info("name=post-init/get_current_cached_cluster/%d:other(%u) result=%s\n",
				nTest, cpu, (testResult ? "PASS" : "FAIL"));

		/*
		 * Switch back to "origin" cluster, ie cluster used at module
		 * loading time
		 */
		cpufreq_driver_target(policy, entry_to_freq(origin_entry),
							CPUFREQ_RELATION_H);
		cpufreq_cpu_put(policy);
	}

	pr_info("name=post-init/get_current_cached_cluster run=%d result=%s pass=%d fail=%d\n",
				nTest, (failCount == 0 ? "PASS" : "FAIL"),
				(nTest - failCount), failCount);
	if (failCount != 0)
		return -1;

	return 0;
}

static int test_cpufreq_driver_target(void)
{
	int nTest = 0, failCount = 0, testResult = 0;
	unsigned int cpu;
	struct cpufreq_frequency_table const *other_entry = NULL;
	struct cpufreq_frequency_table const *origin_entry = NULL;
	struct cpufreq_policy *policy = NULL;

	/*
	 * Try to switch between cluster and check if switch was performed with
	 * success
	 */
	for_each_cpu(cpu, cpu_present_mask) {
		policy = cpufreq_cpu_get(cpu);
		origin_entry = find_entry_by_cluster(get_current_cluster(cpu));
		other_entry = get_other_entry(origin_entry);

		/* Switch to "other" cluster, ie cluster not used at module
		 * loading time
		 */
		cpufreq_driver_target(policy, entry_to_freq(other_entry),
							CPUFREQ_RELATION_H);

		/*
		 * Give the hardware some time to switch between clusters
		 */
		mdelay(SWITCH_DELAY);

		++nTest;
		if (get_current_cluster(cpu) != entry_to_cluster(other_entry)) {
			testResult = 0;
			++failCount;
		} else
			testResult = 1;
		pr_info("name=post-init/cpufreq_driver_target/%d:other(%u) result=%s\n",
				nTest, cpu, (testResult ? "PASS" : "FAIL"));

		/* Switch again to "other" cluster
		 */
		cpufreq_driver_target(policy, entry_to_freq(other_entry),
							CPUFREQ_RELATION_H);
		/*
		 * Give the hardware some time to switch between clusters
		 */
		mdelay(SWITCH_DELAY);

		++nTest;
		if (get_current_cluster(cpu) != entry_to_cluster(other_entry)) {
			testResult = 0;
			++failCount;
		} else
			testResult = 1;
		pr_info("name=post-init/cpufreq_driver_target/%d:otherAgain(%u) result=%s\n",
				nTest, cpu, (testResult ? "PASS" : "FAIL"));

		/* Switch back to "origin" cluster, ie cluster used at module loading
		 * time
		 */
		cpufreq_driver_target(policy, entry_to_freq(origin_entry),
							CPUFREQ_RELATION_H);
		/*
		 * Give the hardware some time to switch between clusters
		 */
		mdelay(SWITCH_DELAY);

		++nTest;
		if (get_current_cluster(cpu) != entry_to_cluster(origin_entry))
		{
			testResult = 0;
			++failCount;
		} else
			testResult = 1;
		pr_info("name=post-init/cpufreq_driver_target/%d:origin(%u) result=%s\n",
				nTest, cpu, (testResult ? "PASS" : "FAIL"));

		/* Switch again to "origin" cluster
		 */
		cpufreq_driver_target(policy, entry_to_freq(origin_entry),
							CPUFREQ_RELATION_H);
		/*
		 * Give the hardware some time to switch between clusters
		 */
		mdelay(SWITCH_DELAY);

		++nTest;
		if (get_current_cluster(cpu) != entry_to_cluster(origin_entry))
		{
			testResult = 0;
			++failCount;
		} else
			testResult = 1;
		pr_info("name=post-init/cpufreq_driver_target/%d:originAgain(%u) result=%s\n",
				nTest, cpu, (testResult ? "PASS" : "FAIL"));

		cpufreq_cpu_put(policy);
	}

	pr_info("name=post-init/cpufreq_driver_target run=%d result=%s pass=%d fail=%d\n",
				nTest, (failCount == 0 ? "PASS" : "FAIL"),
				(nTest - failCount), failCount);
	if (failCount != 0)
		return -1;

	return 0;
}

/* Check that new frequency is expected frequency, increment count and wake up
 * test function.
 */
static int test_arm_bl_cpufreq_notifier(struct notifier_block *nb,
						unsigned long val, void *data)
{
	struct cpufreq_freqs *freq = data;

	if (freq->new != test_transition_freq)
		test_transition_freq = -1;

	++test_transition_count;

	wake_up(&test_wq);

	return 0;
}
static struct notifier_block test_arm_bl_cpufreq_notifier_block = {
	.notifier_call  = test_arm_bl_cpufreq_notifier
};

static int test_transitions(void)
{
	int nTest = 0, failCount = 0, testResult = 0;
	unsigned int cpu, origin_freq, other_freq;
	struct cpufreq_frequency_table const *other_entry = NULL;
	struct cpufreq_frequency_table const *origin_entry = NULL;
	struct cpufreq_policy *policy = NULL;

	/*
	 * register test_arm_bl_cpufreq_notifier_block as notifier :
	 * test_arm_bl_cpufreq_notifier_block will be called on cluster
	 * change and increment transition_count
	 */
	cpufreq_register_notifier(&test_arm_bl_cpufreq_notifier_block,
						CPUFREQ_TRANSITION_NOTIFIER);

	/*
	 * Switch between cluster and check if notifications are received
	 */
	for_each_cpu(cpu, cpu_present_mask) {
		policy = cpufreq_cpu_get(cpu);
		origin_entry = find_entry_by_cluster(get_current_cluster(cpu));
		other_entry = get_other_entry(origin_entry);
		origin_freq = entry_to_freq(origin_entry);
		other_freq = entry_to_freq(other_entry);

		/* Switch on little cluster and check notification
		 */
		++nTest;
		test_transition_count = 0;
		test_transition_freq = other_freq;
		cpufreq_driver_target(policy, other_freq, CPUFREQ_RELATION_H);
		wait_event_timeout(test_wq, (test_transition_count == 2),
				msecs_to_jiffies(SWITCH_TRANSITION_DELAY));

		if ((test_transition_count != 2)
				|| (test_transition_freq != other_freq)) {
			testResult = 0;
			++failCount;
		} else
			testResult = 1;
		pr_info("name=post-init/transitions/%d:other(%u) result=%s\n",
				nTest, cpu, (testResult ? "PASS" : "FAIL"));

		/* Switch on big cluster and check notification
		 */
		++nTest;
		test_transition_count = 0;
		test_transition_freq = origin_freq;
		cpufreq_driver_target(policy, origin_freq, CPUFREQ_RELATION_H);
		wait_event_timeout(test_wq, (test_transition_count == 2),
				msecs_to_jiffies(SWITCH_TRANSITION_DELAY));

		if ((test_transition_count != 2)
				|| (test_transition_freq != origin_freq)) {
			testResult = 0;
			++failCount;
		} else
			testResult = 1;
		pr_info("name=post-init/transitions/%d:origin(%u) result=%s\n",
				nTest, cpu, (testResult ? "PASS" : "FAIL"));

		cpufreq_cpu_put(policy);
	}

	cpufreq_unregister_notifier(&test_arm_bl_cpufreq_notifier_block,
						CPUFREQ_TRANSITION_NOTIFIER);

	pr_info("name=post-init/transitions run=%d result=%s pass=%d fail=%d\n",
				nTest, (failCount == 0 ? "PASS" : "FAIL"),
				(nTest - failCount), failCount);
	if (failCount != 0)
		return -1;

	return 0;
}

static int pre_init_tests(void)
{
	int nTest = 0, failCount = 0;

	pr_info("Begin pre-init tests");

	++nTest;
	if (test_cpufreq_frequency_table() < 0)
		++failCount;

	++nTest;
	if (test_cluster_to_freq() < 0)
		++failCount;

	++nTest;
	if (test_get_current_cluster() < 0)
		++failCount;

	pr_info("name=pre-init run=%d result=%s pass=%d fail=%d\n",
				nTest, (failCount == 0 ? "PASS" : "FAIL"),
				(nTest - failCount), failCount);
	if (failCount != 0)
		return -1;

	return 0;
}

static int post_init_tests(void)
{
	/*
	 * Run all post-init tests
	 *
	 * We wait POST_INIT_TESTS_DELAY ms between tests to be sure system is
	 * in a stable state before running a new test.
	 */
	int nTest = 0, failCount = 0;


	mdelay(POST_INIT_TESTS_DELAY);
	++nTest;
	if (test_cpufreq_driver_target() < 0)
		++failCount;

	mdelay(POST_INIT_TESTS_DELAY);
	++nTest;
	if (test_transitions() < 0)
		++failCount;

	mdelay(POST_INIT_TESTS_DELAY);
	++nTest;
	if (test_get_current_freq() < 0)
		++failCount;

	mdelay(POST_INIT_TESTS_DELAY);
	++nTest;
	if (test_bl_cpufreq_get() < 0)
		++failCount;

	mdelay(POST_INIT_TESTS_DELAY);
	++nTest;
	if (test_get_current_cached_cluster() < 0)
		++failCount;

	pr_info("name=post-init run=%d result=%s pass=%d fail=%d\n",
				nTest, (failCount == 0 ? "PASS" : "FAIL"),
				(nTest - failCount), failCount);
	if (failCount != 0)
		return -1;

	return 0;
}

#undef pr_fmt
#define pr_fmt(fmt) __module_pr_fmt("", fmt)
#else /* ! CONFIG_ARM_BL_CPUFREQ_TEST */

static int pre_init_tests(void) { return 0; }
static int post_init_tests(void) { return 0; }

#endif /* CONFIG_ARM_BL_CPUFREQ_TEST */
#endif /* ARM_BL_CPUFREQ_DEFINE_TESTS */
