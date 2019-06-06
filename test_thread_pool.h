//#ifndef __THREADPOOL__
//#define __THREADPOOL__

#include<pthread.h>
#include<malloc.h>
#include<stdlib.h>
#include<sys/errno.h>
#include<sys/time.h>
#include<sys/types.h>
#include<signal.h>


#define POOL_WAIT      0x1
#define POOL_DESTROY   0x2

/*
                          -----------------------------
                          | Thread Pool Implementation |
                          -----------------------------
Why Thread ?												
Threaded application are suggested where,
1: A task is divided into N subtask and to complete the task significant
   amount of communication is required among the thread.
2: If want to utilize the CPU maximum in case of I/O bound application.	  
	
Creating a thread on demand and destroying it once completeded the task, is not
efficient in cases where a significant amount of task is to be executed by threads.
Every time, creating and destroying a thread is costly. Also creating too many
threads is also in-efficient. Most of the energy is spent in switching the thread
instead of executing the real task. Need to control the maximum thread count also.
Need to maintain minimum number of idle threads wating for task. 
If required, we can maintain an array of such pools. Each pool can be correspond
to each process.
Here we are going to define to C API and it's implementation to support above 
requirements.
*/

/* Queue of jobs */
typedef struct stJob
{
  void* (*job_func)(void*); /* Function to call */
  void* job_arg;            /* Argument passed to function pointed by job_func */
  struct stJob* next_job;
}stJob;

/* List of active threads */
typedef struct stActiveThread
{
   pthread_t thread_id;
   struct stActiveThread* next_active;
}stActiveThread;

/* Data structure for thread pool */
typedef struct stThreadPool
{
   /* List of thread pools */
      struct stThreadPool*   next_pool;

   /* Each pool has its own job queue. Tasks are inserted from
 *       backend and picked from front end */
      stJob*      jb_que_front;
      stJob*      jb_que_back;

   /* Minimum number of threads to be created in pool by default */
      int default_threadcount;

   /* If required, new threads can be increased upto this count on demand */
      int max_threadcount;

  /* If all threads are in idle state for this much of time,
 *      then initiate destroying the excess threads. Excess threads
 *           are those who are created on demand after consumption of
 *                all threads available by default. */
     int max_idlestate_wait_time;

   /* Number of threads in idle state and their ids, waiting for job */
      int current_idle_thread_count;
      stActiveThread*  active_thread_list;
   /* Total number of threads created*/
      int current_worker_thread_count;

   /* Thread attributes, they are initialized by creator of thread pool. */
      pthread_attr_t thrd_attr;

   /* mutex to protect ThreadPool from concurrent access */
      pthread_mutex_t pool_mutex;

   /* Condition on which all threads wait for others to complete.
 *       This is useful in task coordination. */
      pthread_cond_t  wait_other_workers_to_complete_cv;

   /* Condition on which all threads wait for others to terminate.
 *       This is useful in pool destruction. */
      pthread_cond_t  last_thread_to_terminate_cv;

   /* Condition on which worker thread wait for task and if tasks
 *       is availed, then worker is signaled on this condition. Means
 *                 This is useful in communication between job queue and worker
 *                           thread. */
      pthread_cond_t  work_cv;

      int pool_flag;                    /* POOL_WAIT or DESTROY */
}stThreadPool;



/*
                             --------------------
			    | Thread pool APIs: |
			     --------------------
							
Create a global data structure to manage the thread pool.
IN-param : min_thread_count, Minimum number of threads maintained by pool.
IN-param : max_thread_count, Maximum number of threads maintained by pool in
           case of high demand.
IN_param : timeout, Waiting time, If threads are idle till this timeout,
           execss threads will be destroyed.
IN_param : attr, Thread attributes object. location and size of thread stack,
           thread's scheduling policy, priority, joinable/detached and many
		   more can be configured for a thread via this attribute object.
OUT_param: Returns a handle to thread pool data structure */

stThreadPool* CreateThreadPool(unsigned int min_thread_count,
                 unsigned int max_thread_count,
                 unsigned int timeout,
                 pthread_attr_t* attr);	


/*
Create a queue of tasks to be completed. Thread in pool wait for a task request
to be inserted in job queue.	
IN-param : pool, Handle to thread pool. This pool contains task queue where new
		   task request is inserted.
IN-param : func, pointer to a function which represent the task to be done.
IN-param : arg, argument required to be passed to function func.		
*/				
int CreateJobQueue(struct stThreadPool* pool,
               void* (*func)(void*),
   	       void* arg);

/*
Wait till all queued job completed 	
After creating a thread, parent thread does not wait for completion of child.
We need explicit mechanism to wait all the threads till others are running.
IN-param : pool, Handle to thread pool.
*/	   
int WaitTillAllThreadsComplete(struct stThreadPool* pool);

/*
Cancel all queued jobs, destroy the pool, cancell all running task.
IN-param : pool, Handle to thread pool.
*/	
void DestroyPool(struct stThreadPool* pool);

//#endif
