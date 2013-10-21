int soft_realtimeify(void);
int realtimeify(int (*f)(int argc, char *argv[]), int argc, char *argv[]);
int rtfy_set_low_latency(int32_t target);
void rtfy_stop_low_latency(int fd);
int rtfy_set_affin(unsigned int cpu, int num_cpus);
int rtfy_set_max_frequency(int cpu);
int rtfy_set_scaling(int cpu, const char *new_gov);
int rtfy_set_shield(int cpu);
int rtfy_set_sched(int policy);
int rtfy_memlock(void);
