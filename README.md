# Thread pool implementation

Why Thread ?												
Threaded application are suggested where:
1: A task is divided into N subtask and and they can be executed independently by multicore CPUs.
2: If want to utilize the CPU maximum in case of I/O bound application.  
	
Creating a thread on demand and destroying it once completeded the task, is not efficient in cases where a significant amount of task is to be executed by threads. Every time, creating and destroying a thread is costly. Also creating too many threads is also in-efficient. Most of the energy is spent in switching the thread instead of executing the real task. Need to control the maximum thread count also.
Need to maintain minimum number of idle threads wating for task. Here we have defined C API and it's implementation to support above 
requirements.


