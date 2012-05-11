#ifndef ARM_BL_CPUFREQ_H
#define ARM_BL_CPUFREQ_H

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

static unsigned int entry_to_freq(struct cpufreq_frequency_table const *entry);
static unsigned int entry_to_cluster(
				struct cpufreq_frequency_table const *entry);
static struct cpufreq_frequency_table const *find_entry_by_cluster(int cluster);
static unsigned int cluster_to_freq(int cluster);
static int get_current_cluster(unsigned int cpu);
static int get_current_cached_cluster(unsigned int cpu);
static unsigned int get_current_freq(unsigned int cpu);
static unsigned int bl_cpufreq_get(unsigned int cpu);

#endif /* ! ARM_BL_CPUFREQ_H */
