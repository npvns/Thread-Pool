#ifndef __THREADPOOL__
#define __THREADPOOL__

#include<pthread.h>
#include<malloc.h>
#include<stdlib.h>
#include<sys/errno.h>
#include<sys/time.h>
#include<sys/types.h>
#include<signal.h>
#include<unistd.h>
#include<sys/wait.h>

/* This is set, when a thread worker wants to wait for other thread workers to complete before exit.
#define POOL_WAIT      0x1   

/* This is set, when pool is being destroyed */
#define POOL_DESTROY   0x2   

                          /*---------------------------------------------------
                          | Data Structure used for Thread Pool Implementation |
                           ---------------------------------------------------*/
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

/* Data structure for thread pool handle*/
typedef struct stThreadPool
{
     /* job_count, pool_count and pool_id are used for testing the code.
        They are not part of the data structure. */	
     int job_count;        
     int pool_count;	
     struct stThreadPool* pool_id; 	
	
   /* List of thread pools */
      struct stThreadPool*   next_pool;
       
   /* Each pool has its own job queue. Tasks are inserted from backend and picked from front end. */
      stJob*      jb_que_front;
      stJob*      jb_que_back;

   /* Minimum number of threads to be created in pool by default. */
      int default_threadcount;

   /* If required, new threads can be increased upto this count on demand. */
      int max_threadcount;

  /* If all threads are in idle state for this much of time, then initiate destroying the excess threads.
     Excess threads are those who are created on demand after consumption of all threads available by
     default. */
     int max_idlestate_wait_time;

   /* Number of threads in idle state and their thread ids, waiting for job. */
      int current_idle_thread_count;
      stActiveThread*  active_thread_list;

   /* Total number of threads created. */
      int current_worker_thread_count;

   /* Thread attributes, they are initialized by creator of thread pool. */
      pthread_attr_t thrd_attr;

   /* Mutex to protect ThreadPool data structure from concurrent access. */
      pthread_mutex_t pool_mutex;

   /* Condition on which all threads wait for others to complete. This is useful in task coordination. */
      pthread_cond_t  wait_other_workers_to_complete_cv;

   /* Condition on which all threads wait for others to terminate. This is useful in pool destruction. */
      pthread_cond_t  last_thread_to_terminate_cv;

   /* Condition on which worker thread wait for task and if tasks is availed, then worker is signaled
      on this condition. Means This is useful in communication between job queue and worker thread. */
      pthread_cond_t  work_cv;

      int pool_flag;                    /* POOL_WAIT or DESTROY */
}stThreadPool;


                            /*-------------------
			    | Thread pool APIs: |
			     ------------------*/
/*
Create a global thread pool handle to manage the thread pool.
IN-param : min_thread_count, Minimum number of threads maintained by pool.
IN-param : max_thread_count, Maximum number of threads maintained by pool in case of high demand.
IN_param : timeout, Waiting time, If threads are idle till this timeout, execss threads will be destroyed.
IN_param : attr, Thread attributes object. location and size of thread stack, thread's scheduling policy,
           priority, joinable/detached and many more can be configured for a thread via this attribute object.
OUT_param: Returns a handle to thread pool. */
stThreadPool* CreateThreadPool(unsigned int min_thread_count,
                               unsigned int max_thread_count,
                               unsigned int timeout,
                               pthread_attr_t* attr);	


/*
Create a queue of tasks to be completed. Threads in pool wait for a task request to be inserted in job queue.	
IN-param : pool, Handle to thread pool. This pool contains task queue where new task request is inserted.
IN-param : func, pointer to a function which represent the task to be done.
IN-param : arg, argument required to be passed to function func.		
*/				
int CreateJobQueue(struct stThreadPool* pool,
                   void* (*func)(void*),
   	           void* arg);

/*
Wait till all queued jobs completed. 	
After creating a thread, parent thread does not wait for completion of child. We need explicit mechanism
to wait for other threads to be completed.
IN-param : pool, Handle to thread pool.
*/	   
int WaitTillAllThreadsComplete(struct stThreadPool* pool);

/*
Cancel all queued jobs, destroy the pool, cancell all running task.
IN-param : pool, Handle to thread pool.
*/	
void DestroyPool(struct stThreadPool* pool);

#endif
