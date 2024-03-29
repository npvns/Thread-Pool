#include "thread_pool.h"

/* Global list of handles for thread pools. */
   static stThreadPool* global_pool_list = NULL;

/* Global mutex to protect thread pool from concurrent access by worker threads. */
   static pthread_mutex_t global_pool_list_lock = PTHREAD_MUTEX_INITIALIZER;

/* Global set of all signals. */
   static sigset_t global_fillset;
   
					 /*--------------------
					 | API Implementation |
					 --------------------*/

/* If I am the last worker and nothing in job queue, signal all other workers who are waiting
   for the last worker to be completed. */
static void NotifyWaiters(stThreadPool* pool)
{
  if(pool->jb_que_front == NULL && pool->active_thread_list == NULL)
  {
    pool->pool_flag = pool->pool_flag & ~POOL_WAIT;
    pthread_cond_broadcast(&pool->wait_other_workers_to_complete_cv);
  }
}

/* Called by worker thread after returning from job. Remove my entry from active list. */
static void JobCleanup(stThreadPool* pool)
{
   pthread_t my_thread_id = pthread_self();
   stActiveThread*  actp;
   stActiveThread**  actpp;

   pthread_mutex_lock(&pool->pool_mutex);
   for(actpp = &pool->active_thread_list; (actp = *actpp) != NULL;
       actpp = &actp->next_active )
   {
      if(actp->thread_id == my_thread_id)
      {
         *actpp = actp->next_active;
         break;
      }
   }
/* POOL_WAIT is set, means some one is wating for last worker to be completed. NotifyWaiters
   wakes up all workers who are wating, if I am the last worker. */
   if(pool->pool_flag & POOL_WAIT)
      NotifyWaiters(pool);
}

/* CreateNewWorker creates a new worker thread, update the thread pool handle for this new
   new thread, and then always execute function MasterWorkerFunction. This function in turns
   pick a task from job queue and execute it. */
static void* MasterWorkerFunction(void* arg);
int CreateNewWorker(stThreadPool* pool)
{
  sigset_t oset;
  int error;
  pthread_t t1;
  pthread_sigmask(SIG_SETMASK, &global_fillset, &oset);
  error = pthread_create(&t1, &pool->thrd_attr, MasterWorkerFunction, pool);
  pthread_sigmask(SIG_SETMASK, &oset, NULL);
  return error;
}

/* Cleanup the threads and resources occupied by them, in case of thread termination.
   Possible reasons may be:
   1: Excess threads can be gracefully terminated if they are not needed.
   2: Threads were cancelled due to some error.
   3: Destroy the thread pool, if they are not needed or system is shutting down.
   4: Job function calls pthread_exit. */
static void WorkerCleanup(stThreadPool* pool)
{
  pool->current_worker_thread_count--;
  if(pool->pool_flag & POOL_DESTROY)
  {
    if(pool->current_worker_thread_count == 0)
        {
           pthread_cond_broadcast(&pool->last_thread_to_terminate_cv);
        }
  }
  else if(pool->jb_que_front != NULL &&
              pool->current_worker_thread_count < pool->max_threadcount)
  {
           int r = CreateNewWorker(pool);
  }
  pthread_mutex_unlock(&pool->pool_mutex);
}

/* Create job queue. After enqueing a job, it's assigned to some thread waiting for task
   OR a new thread is created if no worker is available to pick it. Parent thread does
   not wait for child to complete. It will enque the job and move ahead. */
int CreateJobQueue(stThreadPool* pool, 
                   void* (*func)(void*),
                   void* arg)
{
   stJob* job = malloc(sizeof(stJob));
   memset(job, 0, sizeof(stJob));
   job->next_job = NULL;
   job->job_func = func;
   job->job_arg = arg;
   int ret_code = 0;
    
   pthread_mutex_lock(&pool->pool_mutex);
   pool->job_count++;
   if(pool->jb_que_front == NULL)
   {
      pool->jb_que_front = job;
   }
   else
   {
      pool->jb_que_back->next_job = job;
   }
   pool->jb_que_back = job;
   
   if(pool->current_idle_thread_count > 0)
   {	   
      pthread_cond_signal(&pool->work_cv);
   }	   
   else if(pool->current_worker_thread_count < pool->max_threadcount)
   {
       ret_code = CreateNewWorker(pool);
       pool->current_worker_thread_count++;
   }   
   pthread_mutex_unlock(&pool->pool_mutex);
   return 0;
}

/* Copy user supplied thread attributes into thread pool handle. Same attribute is
   passed to pthread_create. */
static void CloneAttributes(pthread_attr_t* copy_attr_to_pool,
                            pthread_attr_t* attr)
{
  void* addr;
  size_t size;
  int value;
  struct sched_param param;
  printf("CloneAttributes\n");
  pthread_attr_init(copy_attr_to_pool);

  pthread_attr_getstack(attr, &addr, &size);
  pthread_attr_setstack(copy_attr_to_pool, NULL, size);

  pthread_attr_getscope(attr, &value);
  pthread_attr_setscope(copy_attr_to_pool, value);

  pthread_attr_getinheritsched(attr, &value);
  pthread_attr_setinheritsched(copy_attr_to_pool, value);

  pthread_attr_getschedpolicy(attr, &value);
  pthread_attr_setschedpolicy(copy_attr_to_pool, value);

  pthread_attr_getschedparam(attr, &param);
  pthread_attr_setschedparam(copy_attr_to_pool, &param);

  pthread_attr_getguardsize(attr, &size);
  pthread_attr_setguardsize(copy_attr_to_pool, size);
}


/* Create thread pool handle, pool is created on heap, then pool is added into global
   list global_pool. Global list update is mutex protected.   */
stThreadPool* CreateThreadPool(unsigned int min_thread,
                               unsigned int max_thread,
	                       unsigned int max_idle_wait_time,
	                       pthread_attr_t* attr)
{
 /* Initialize global signal set global_fillset with all signals*/ 
   sigfillset(&global_fillset);
	
   if(min_thread > max_thread ||  max_thread < 1)
   {
     errno = EINVAL;
     return NULL;
   }   
   stThreadPool* pool = malloc(sizeof(stThreadPool));
   memset(pool, 0, sizeof(stThreadPool));
	
   pool->max_idlestate_wait_time = max_idle_wait_time;
   pool->max_threadcount = max_thread;
   pool->default_threadcount = min_thread;
	
/* pthread_mutexattr_t is NULL, mutex is initialized with defaults attributes.
   One of attribute may be PTHREAD_MUTEX_NORMAL, PTHREAD_MUTEX_ERRORCHECK, 
   PTHREAD_MUTEX_RECURSIVE. */
   pthread_mutex_init(&pool->pool_mutex, NULL);  
	
/* Condition variables are initialized with default attributes */
   pthread_cond_init(&pool->wait_other_workers_to_complete_cv, NULL);
   pthread_cond_init(&pool->last_thread_to_terminate_cv, NULL);
   pthread_cond_init(&pool->work_cv, NULL);

/* Initialize the pool->attr with user supplied attributes thread attributes object.
   pool->attr is passed to pthread_create. */	
   if(attr != NULL)
      CloneAttributes(&pool->thrd_attr, attr);
	
/* Insert this newly create pool into global pool list */
   pthread_mutex_lock(&global_pool_list_lock);
   if(global_pool_list == NULL)
  {
    global_pool_list = pool;
  }
  else
  {
    pool->next_pool = global_pool_list;
    global_pool_list = pool;
  } 
  pthread_mutex_unlock(&global_pool_list_lock);	
  return pool;
}	
	
/* Master worker function. This is the entry point for newly created threads. */
static void* MasterWorkerFunction(void* arg)
{
  stThreadPool* pool = (stThreadPool*)arg;
  unsigned int timedout;
  stJob* job;
  void* (*func)(void*);
  stActiveThread active;
  struct timespec ts;
  pthread_mutex_lock(&pool->pool_mutex);
/* Add WorkerCleanup function on top of calling thread's stack of cleanup
   handlers. */ 
  pthread_cleanup_push(WorkerCleanup, pool);
  active.thread_id = pthread_self();
  
/* This is workers main loop. Worker thread breaks from this loop exit in case of: 
   1: If thread waited for max defined wait time and no task is availed.
   2: Due to some reason, thread pool is being destroyed. */	   
  for(;;)
  {
  /* Reset signal mask, cancellation state, because these state may be changed to
     some other state while thread was completing its last assignment. */
     pthread_sigmask(SIG_SETMASK, &global_fillset, NULL);
	   
  /* Deferred the cancel request till cancellation point. Cancellation point is a
     set of functions provided by implementation. Most of thease are the functions
     capable of blocking the the thread for an indefinite period of time. */
     pthread_setcanceltype(PTHREAD_CANCEL_DEFERRED, NULL); 
	   
   /* Make thread cancellable */
      pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);  
		   
      timedout = 0;
	   
   /* If a thread is here, means thread has become idle. */
      pool->current_idle_thread_count;
	   
   /* POOL_WAIT is set when
      1: A thread calls ThreadPoolWait to wait for other threads to complete before exiting
         itself.
      2: In case of cancellation of all running threads, all running threads wait for each
         other to complete the cancellation process. It means in both cases, last thread
	 will wakeup all others who are waiting. Whenever a thread loops back, or created
	 first time, it calls NotifyWaiters to check if I am the last worker and there is
	 nothing to in job queue or in running state, then signal all waiting threads to
	 go ahead for exit/cleanup process.
    */	  
    if(pool->pool_flag & POOL_WAIT)
    {
      NotifyWaiters(pool);
    }
    /* If we are here, it means
       1: There is some work in job queue OR 
       2: There is some thead running.
    */   
    while(pool->jb_que_front == NULL && !(pool->pool_flag & POOL_DESTROY))
    {
     /* If we are here, it means their is no work in job queue. */
        if(pool->current_worker_thread_count <= pool->default_threadcount)
        {
          /* Just wait for work to be available*/
             pthread_cond_wait(&pool->work_cv, &pool->pool_mutex);
        }
        else
        {
         /* If we are here, it means worker thread count is greater than the minimum default
	    thread count defined. Set timedout. Timedout tells that worker threads have waited
	    enough, now excess workers can be asked to exit and keep only default minimum 
	    worker threads to wait for work. Be economical.
        */
        clock_gettime(CLOCK_REALTIME, &ts);
	ts.tv_sec += pool->max_idlestate_wait_time;
	int r = pthread_cond_timedwait(&pool->work_cv, &pool->pool_mutex, &ts);
	if(r == ETIMEDOUT) { timedout = 1; break; } 
      }
    }
	
/* If we are here, it means an idle thread is assigned a task. */
   pool->current_idle_thread_count--;
		   
/* Before picking the task check if cancel request is submitted, if yes, break from loop. */
   if(pool->pool_flag & POOL_DESTROY) break;
		   
/* Pick the task */
   job = pool->jb_que_front;
   if(job != NULL)
   {
     timedout = 0;
     func = job->job_func;
     arg = job->job_arg;
     /* Got a new job */
     pool->jb_que_front = job->next_job;
     if(job == pool->jb_que_back)
     {
       pool->jb_que_back = NULL;
       pool->jb_que_front = NULL;
       
     }
    /* Woker became active now */
       active.next_active = pool->active_thread_list;
       pool->active_thread_list = &active;
    /* ThreadPool shared data structure is updated, now unlcok the pool mutex. */	  
       pthread_mutex_unlock(&pool->pool_mutex);
			  
    /* Before executing the actual task, install JobCleanup routine.This cleanup
       routine is called in any case if function calls pthread_exit.*/
       pthread_cleanup_push(JobCleanup, pool);

    /* Calls the speicified job function */
       free(job);
       pool->job_count--;
       func(arg);
	  
    /* Job is done, Pop the function JobCleanup, execute JobCleanup, it will remove me
       from active thread list */
       pthread_cleanup_pop(1);
    }
	
   /* Timedout, means nothing to do and worker are in excess. Exit now to reduce
      the size of pool. */
      if(timedout && pool->current_worker_thread_count > pool->default_threadcount)
      {
	   break;
      } 		   
  }
/* If we are here, means thread is going to exit. Pop WokerCleanup, execute WokerCleanup.
   Adjust the number of threads according to need.
   If worker thread is in excess, terminate the excess one.
   If there is shortage, create new one.
   If POOL_DESTROY is enabled and I am the last, notify others to go ahead and destroy the pool. */
  pthread_cleanup_pop(1);
  return NULL;
}

/* If a thread has completed it's task, it checks if there are smething pending in job queue or
   some other threads are running, if yes it waits for others to complete on condition 
   wait_other_worker_to_complete_cv before exit. It is similar ot pthread_join. Caller of
   pthread_create is CreateJobQueue and parent thread executing CreateJobQueue should not wait
   till completion of the child threads executing the task. Child threads coordinates themselves
   to complete the task. Hence threads need to explicitly call ThreadPoolWait to facilitate
   wait. The last worker completing the task, notify all others by broadcasting on conditioin
   wait_other_worker_to_complete_cv. */
static void ThreadPoolWait(stThreadPool* pool)
{
  pthread_mutex_lock(&pool->pool_mutex);
  pthread_cleanup_push(pthread_mutex_unlock, &pool->pool_mutex);
  while(pool->jb_que_front != NULL || pool->active_thread_list != NULL)
  {
    pool->pool_flag = pool->pool_flag | POOL_WAIT;
	pthread_cond_wait(&pool->wait_other_workers_to_complete_cv, &pool->pool_mutex);
  }
  pthread_cleanup_pop(1);   
}


/* Destory the pool, remove it's entry from global pool and free the resources */
void ThreadPoolDestroy(stThreadPool* pool)
{
  stActiveThread* act;
  stThreadPool* global_pool_pointer;
  stThreadPool* temp;
  stJob *job;
   
   pthread_mutex_lock(&pool->pool_mutex);  
/* Mark the pool as being destroyed and wake up the idle workers. */
   pool->pool_flag |= POOL_DESTROY;
   pthread_cleanup_push(pthread_mutex_unlock, &pool->pool_mutex);
   
/* cancell all active workers. */
   for(act = pool->active_thread_list; act != NULL; act = act->next_active)
   {
      pthread_cancel(act->thread_id);
   } 
   
/* Wait for all active worker to finish. */
   while(pool->active_thread_list != NULL)
   {
      pool->pool_flag |= POOL_WAIT;
      pthread_cond_wait(&pool->wait_other_workers_to_complete_cv, &pool->pool_mutex);
   }
   
/* Last worker to terminate will wake-up us. */
   while(pool->current_worker_thread_count != 0)
         pthread_cond_wait(&pool->last_thread_to_terminate_cv, &pool->pool_mutex);
	
/* All threads terminated, ThreadPool data structure is updated accoedingly, release the lock. */
   pthread_cleanup_pop(1);	
   
/* Detach the pool from global pool list. */
   pthread_mutex_lock(&global_pool_list_lock);
   global_pool_pointer = global_pool_list;
   if(global_pool_list == pool)
   {
     free(global_pool_pointer);
     global_pool_list = NULL;
   }
   else
   {   
     global_pool_pointer = global_pool_list;
     while(global_pool_pointer->next_pool != pool)
     {
      global_pool_pointer = global_pool_pointer->next_pool;
     }
     temp = global_pool_pointer->next_pool;
     global_pool_pointer->next_pool = global_pool_pointer->next_pool->next_pool;
     free(temp); 
   }  
   pthread_mutex_unlock(&global_pool_list_lock);

/* There should be no pendingg task, but just in case. */
   for(job = pool->jb_que_front; job != NULL; job = pool->jb_que_front)
   {
      pool->jb_que_front = job->next_job;
//      free(job); /* Some issue is here, as of now commenting, debugging this */
   }
   pthread_attr_destroy(&pool->thrd_attr);
}

                                      /*------------------------
                                      | Test related functions |
				       -----------------------*/
/* Task executed by worker threads.
    It simply prints pool id, thread id, process id, at any point number of jobs
    in queue. */
void* routine_task(void* in)
{
  stThreadPool* tp = (stThreadPool*)in;
  pthread_t my_thread_id = pthread_self();
  pid_t my_process_id = getpid();
  printf("I am routine task\n");	
  printf("pool_id= %p  job_count= %d my_thread_id= %d my_process_id= %d\n",tp->pool_id, tp->job_count, my_thread_id, my_process_id);
  return NULL;
}

void* special_task(void* in)
{
  stThreadPool* tp = (stThreadPool*)in;
  pthread_t my_thread_id = pthread_self();
  pid_t my_process_id = getpid();
  printf("I am special task\n");
  printf("pool_id= %p  job_count= %d my_thread_id= %d my_process_id= %d\n",tp->pool_id, tp->job_count, my_thread_id, my_process_id);
   return NULL;
}

int main()
{
   /* These values are used to create a normal worker thread. */	
   unsigned int min = 100;
   unsigned int max = 1000;
   unsigned int tm_out = 10;

   /* These values are used to create a special worker thread.	
   unsigned int dedicated_min_thread = 5;
   unsigned int dedicated_max_thread = 5;	
   int i;
   fork();fork();fork();fork();

   /* Threads in this pool peroform the routine work protecting the data. */	
   stThreadPool* pool_handle = CreateThreadPool(min, max, tm_out, NULL);

    /* Threads in this pool perform special task. They can be used for loging purpose
       OR cleanup in case of abnormal termination of other threads OR to monitor the 
       disk/cpu/memory consumption and many more. */	
   stThreadPool* dedicated_pool_handle = CreateThreadPool(dedicated_min_thread, dedicated_max_thread, 0, NULL);	

   /* Note: Though pool_handle and dedicated_pool_handle can be linked, but as of now keeping them separate */	
   pool_handle->pool_id = pool_handle;
   dedicated_pool_handle->pool_id = dedicated_pool_handle;	
   while(1)
   {
     /* Wait if job queue is filled till some defined threshold number of jobs. 
        This is done to make a balance between producer and consumer. If it's 
	controlled, job queue can occupy whole available main memory. Another
	way to balance is to create enough number of worker threads to pick
	the task from queue. But again huge number of threads can consume main
	memory. Need to define an optimized number for:
	1: Maximum number of threads allowed to be created.
	2: Maximum number of tasks which can be queued at any point of time. Beyond that task
	   producer will have to wait till some sapce is availed in jib queue. */	   
     if(pool_handle->job_count > 500000)
     {	     
	sleep(1);
	CreateJobQueue(dedicated_pool_handle, special_task, dedicated_pool_handle);     
     }	     
     CreateJobQueue(pool_handle, print_data, pool_handle);    
   }
  /* Wait till all done. */	
  ThreadPoolWait(pool_handle);
  ThreadPoolWait(dedicated_pool_handle);	

  /* All done, destroy the pool and free the resources. */	
  ThreadPoolDestroy(pool_handle);
  ThreadPoolDestroy(dedicated_pool_handle);
  return 0;
}
