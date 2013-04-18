#define _GNU_SOURCE
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdlib.h>
#include <sched.h>
#include <sys/io.h>
#include <sys/mman.h>

/* Some APIs use 0 to indicate the current process. */
#define THIS_PID	0

static int rtfy_set_low_latency(int32_t target) {
	int pm_qos_fd = open("/dev/cpu_dma_latency", O_RDWR);
	if (pm_qos_fd < 0) {
		fprintf(stderr, "Failed to open PM QOS file: %s",
		strerror(errno));
		return pm_qos_fd;
	}
	if (write(pm_qos_fd, &target, sizeof(target)) != sizeof(target))
		fprintf(stderr, "Failed to write to PM QOS file.\n");

	return pm_qos_fd;
}

static void rtfy_stop_low_latency(int fd) {
	if (fd >= 0)
		close(fd);
}

static int get_num_cpus(void) {
	int num_cpus = sysconf(_SC_NPROCESSORS_ONLN);
	if (num_cpus <= 0) {
		perror("getting _SC_NPROCESSORS_ONLN failed");
	}
	return num_cpus;
}

static int rtfy_set_affin(unsigned int cpu, int num_cpus) {
	cpu_set_t *mask;
	size_t size;

	if (num_cpus <= 0)
		num_cpus = get_num_cpus();

	if (num_cpus <= 0)
		return EXIT_FAILURE;

	mask = CPU_ALLOC(num_cpus);
	if (mask == NULL) {
		perror("CPU_ALLOC failed");
		return EXIT_FAILURE;
	}

	size = CPU_ALLOC_SIZE(num_cpus);
	CPU_ZERO_S(size, mask);
	CPU_SET_S(cpu, size, mask);

	if(sched_setaffinity(THIS_PID, size, mask) != 0) {
		perror("sched_setaffinity failed");
		return EXIT_FAILURE;
	}

	return EXIT_SUCCESS;
}

static int get_scaling(int cpu, char **gov) {
	char buf[64];
	int fd;
	ssize_t read_cnt;
	char *tmp;
	int ret = EXIT_SUCCESS;
	snprintf(buf, sizeof(buf), "/sys/devices/system/cpu/cpu%d/cpufreq/scaling_governor", cpu);

	fd = open(buf, O_RDWR);
	if (fd < 0) {
		fprintf(stderr, "Failed to open cpufreq file %s: %s", buf, strerror(errno));
		return EXIT_FAILURE;
	}

	read_cnt = read(fd, buf, sizeof(buf));
	if (read_cnt < 0) {
		fprintf(stderr, "Failed to read from cpufreq file.\n");
		ret = EXIT_FAILURE;
		goto out;
	}
	buf[sizeof(buf) - 1] = '\0';

	tmp = malloc(strlen(buf) + 1);
	if (tmp == NULL) {
		fprintf(stderr, "No memory.\n");
		ret = EXIT_FAILURE;
		goto out;
	}
	strcpy(tmp, buf);
	*gov = tmp;
out:
	close(fd);
	return ret;
}

static int rtfy_set_max_frequency(int cpu) {
	int ret;
	char buf[128];
	snprintf(buf,
			 sizeof(buf),
			 "cat /sys/devices/system/cpu/cpu%d/cpufreq/cpuinfo_max_freq > /sys/devices/system/cpu/cpu%d/cpufreq/scaling_setspeed",
			 cpu,
			 cpu);
	ret = system(buf);
	if (ret != 0)
		return EXIT_FAILURE;
	return EXIT_SUCCESS;
}

static int rtfy_set_scaling(int cpu, const char *new_gov) {
	char buf[64];
	int fd;
	int ret = EXIT_SUCCESS;
	snprintf(buf, sizeof(buf), "/sys/devices/system/cpu/cpu%d/cpufreq/scaling_governor", cpu);

	fd = open(buf, O_RDWR);
	if (fd < 0) {
		fprintf(stderr, "Failed to open cpufreq file %s: %s", buf, strerror(errno));
		return EXIT_FAILURE;
	}

	if (write(fd, new_gov, strlen(new_gov)) != strlen(new_gov)) {
		fprintf(stderr, "Failed to write to cpufreq file.\n");
		ret = EXIT_FAILURE;
	}

	close(fd);
	return ret;
}

static int rtfy_set_shield(int cpu) {
	char buf[64];
	int ret;

	snprintf(buf, sizeof(buf), "cset shield -c %d,%d -k on > /dev/null", cpu, cpu+1);
	ret = system(buf);
	if (ret != 0)
		return EXIT_FAILURE;

	snprintf(buf, sizeof(buf), "cset shield --shield --pid %d > /dev/null", getpid());
	ret = system(buf);
	if (ret != 0)
		return EXIT_FAILURE;

	return EXIT_SUCCESS;
}

static int rtfy_set_sched(int policy) {
	struct sched_param param;

	if (sched_getparam(THIS_PID, &param) < 0) {
		perror("sched_getparam failed");
		return EXIT_FAILURE;
	}

	param.sched_priority = sched_get_priority_max(policy);
	if (param.sched_priority < 0) {
		perror("sched_get_priority_max failed");
		return EXIT_FAILURE;
	}

	if (sched_setscheduler(THIS_PID, policy, &param) != 0) {
		perror("sched_setscheduler failed");
		return EXIT_FAILURE;
	}

	return EXIT_SUCCESS;
}

static int rtfy_memlock(void) {
	if (mlockall(MCL_CURRENT | MCL_FUTURE) != 0)
		return EXIT_FAILURE;
	else
		return EXIT_SUCCESS;
}

int soft_realtimeify(void) {
	if (getuid() != 0)
		return EXIT_FAILURE;

	if (rtfy_memlock() != EXIT_SUCCESS)
		return EXIT_FAILURE;

	if (rtfy_set_affin(0, 0) != EXIT_SUCCESS)
		return EXIT_FAILURE;

	if (rtfy_set_sched(SCHED_FIFO) != EXIT_SUCCESS)
		return EXIT_FAILURE;

	return EXIT_SUCCESS;
}

int realtimeify(int (*f)(int argc, char *argv[]), int argc, char *argv[]) {
	char *prev_gov;
	unsigned int target_cpu;
	int num_cpus;
	int pm_qos_fd;
	int ret;
	int tear_down_ret;

	if (getuid() != 0) {
		fprintf(stderr, "ERROR: You need to be root.\n");
		return EXIT_FAILURE;
	}

	if (rtfy_memlock() != EXIT_SUCCESS) {
		perror("mlockall");
		return EXIT_FAILURE;
	}
	printf("Current and future memory locked.\n");

	num_cpus = get_num_cpus();
	if (num_cpus <= 0)
		return EXIT_FAILURE;
	target_cpu = num_cpus/2;
	
	if (rtfy_set_shield(target_cpu) != EXIT_SUCCESS) {
		fprintf(stderr, "ERROR: Could not shield target core.\n");
		return EXIT_FAILURE;
	}
	printf("Target core %d shielded.\n", target_cpu);

	if (rtfy_set_affin(target_cpu, num_cpus) != EXIT_SUCCESS) {
		fprintf(stderr, "ERROR: Could not pin process to target core.\n");
		return EXIT_FAILURE;
	}
	printf("Process pinned to target core.\n");

	if (rtfy_set_sched(SCHED_FIFO) != EXIT_SUCCESS) {
		fprintf(stderr, "ERROR: Could not reschedule process with real-time priority.\n");
		return EXIT_FAILURE;
	}
	printf("Rescheduled to real-time priority.\n");

	if (get_scaling(target_cpu, &prev_gov) != EXIT_SUCCESS) {
		fprintf(stderr, "ERROR: Could not get current CPU frequency scaling governor.\n");
		return EXIT_FAILURE;
	}
	if (rtfy_set_scaling(target_cpu, "userspace") != EXIT_SUCCESS) {
		fprintf(stderr, "ERROR: Could not set current CPU frequency scaling governor.\n");
		return EXIT_FAILURE;
	}
	if (rtfy_set_max_frequency(target_cpu)) {
		fprintf(stderr, "ERROR: Could not set CPU frequency.\n");
		return EXIT_FAILURE;
	}
	printf("CPU frequency scaling disabled.\n");

	pm_qos_fd = rtfy_set_low_latency(0);

	ret = f(argc, argv);
	tear_down_ret = EXIT_SUCCESS;

	rtfy_stop_low_latency(pm_qos_fd);
	munlockall();
	if (rtfy_set_scaling(target_cpu, prev_gov) != EXIT_SUCCESS) {
		fprintf(stderr, "ERROR: Could not reenable CPU frequency scaling.\n");
		tear_down_ret = EXIT_FAILURE;
	}
	if (system("cset shield -r > /dev/null") != 0) {
		fprintf(stderr, "ERROR: Could not disable CPU core shielding.\n");
		tear_down_ret = EXIT_FAILURE;
	}

	if (tear_down_ret != EXIT_SUCCESS)
		return EXIT_FAILURE;
	
	return ret;
}
