# A tiny library (that tries) to make Linux user-space code realtime-capable

Existing code can be wrapped by this library to let it run in an environment more suitable for realtime applications. Using this is by no means suitable for *genuine* realtime application but might help to evalute code quickly regarding its realtime capabilities by getting disturbing stuff out of the way. It is advisable to use this with a non-generic realtime-aware kernel like the -lowlatency Ubuntu kernels or a fully -RT patched kernel.

## Features
* prevents page faults by locking all memory.  
  via ```mlockall(MCL_CURRENT | MCL_FUTURE)```  
  see ```mlockall(2)```, ```mlockall(3posix)```

* disables frequency scaling  
  by setting ```/sys/devices/system/cpu/cpuX/cpufreq/scaling_governor``` to ```userspace``` and copying the maximum possible frequency to ```/sys/devices/system/cpu/cpuX/cpufreq/scaling_setspeed```  
  see https://www.kernel.org/doc/Documentation/cpu-freq/governors.txt and https://www.kernel.org/doc/Documentation/cpu-freq/user-guide.txt

* disables idle states  
  via ```/dev/cpu_dma_latency```  
  see https://www.kernel.org/doc/Documentation/power/pm_qos_interface.txt, https://access.redhat.com/knowledge/articles/65410

* shields all processes on one core (moves all others away)  
  via ```cset```  
  see ```cset(1)```, ```cset-set(1)```, ```cset-proc(1)```, ```cset-shield(1)```

* reschedules the process onto said core with FIFO realtime policy  
  ```see sched_setscheduler(2)```, ```sched_setscheduler (3posix)```
  
* runs the given function

* reverts all temporary changes above

* alternatively, soft_realtimeify() can be used to engage a subset of above without drastic consequences on performance and power usage. It locks memory, uses realtime scheduling and fixates the affinity of the calling thread.

## Usage
	int benchmark(int argc, char **argv) {
		// allocate any memory used below
		// realtime code
		return EXIT_SUCCESS;
	}
	
	int main(int argc, char **argv) {
		return realtimeify(benchmark, argc, argv);
	}
