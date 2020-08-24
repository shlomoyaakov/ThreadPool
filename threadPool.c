
#include "threadPool.h"

void *run(void *threadPool);
void freeThreadPool(ThreadPool *);
void errorHandler(Error error, ThreadPool *);
bool validateThreadPoolForInsert(ThreadPool *);

ThreadPool *tpCreate(int numOfThreads) {
  int i;
  //Initialize the thread pool.
  ThreadPool *newThreadPool = (ThreadPool *) malloc(sizeof(ThreadPool));
  if (newThreadPool == NULL) {
    errorHandler(Malloc, NULL);
    exit(-1);
  }
  newThreadPool->queue = NULL;
  newThreadPool->ntid = NULL;

  //Initialize the fields inside the thread pool:
  newThreadPool->ntid = (pthread_t *) malloc(numOfThreads * sizeof(pthread_t));
  if (newThreadPool->ntid == NULL)
    errorHandler(Malloc, newThreadPool);
  newThreadPool->queue = osCreateQueue();
  if (newThreadPool->queue == NULL)
    errorHandler(Malloc, newThreadPool);

  if ((pthread_mutex_init(&newThreadPool->lock, NULL)) != 0)
    errorHandler(Mutex_Init, newThreadPool);

  if ((pthread_cond_init(&newThreadPool->cond, NULL)) != 0)
    errorHandler(Cond_Init, newThreadPool);

  newThreadPool->numOfThreads = numOfThreads;
  newThreadPool->beingDestroyed = false;
  newThreadPool->canExecute = true;

  //Creates the threads in the thread pool.
  for (i = 0; i < numOfThreads; i++) {
    if((pthread_create(&(newThreadPool->ntid[i]), NULL, run, (void *) newThreadPool))!=0)
      errorHandler(Pthread_Create,newThreadPool);
  }
  return newThreadPool;
}

int tpInsertTask(ThreadPool *threadPool, void (*computeFunc)(void *), void *param) {
  //Validate that the thread pool and the queue inside the thread pool aren't null
  if (!validateThreadPoolForInsert(threadPool))
    return -1;
  //acquire the lock
  if((pthread_mutex_lock(&threadPool->lock))!=0)
    errorHandler(Lock,threadPool);
  if (threadPool->beingDestroyed == true)
    return -1;
  if((pthread_mutex_unlock(&threadPool->lock))!=0)
    errorHandler(Unlock,threadPool);

  //Create the task that the threads are going to run
  Task *task = (Task *) malloc(sizeof(Task));
  if (task == NULL)
    errorHandler(Malloc,threadPool);
  task->computeFunc = computeFunc;
  task->param = param;
  //and then insert it to the queue inside the thread pool
  if((pthread_mutex_lock(&threadPool->lock))!=0)
    errorHandler(Lock,threadPool);
  osEnqueue(threadPool->queue, task);
  //cond signal that awake one of the threads that were sleeping because of the cond.
  if((pthread_cond_signal(&threadPool->cond))!=0)
    errorHandler(Cond_Signal,threadPool);
  if((pthread_mutex_unlock(&threadPool->lock))!=0)
    errorHandler(Unlock,threadPool);
}
/*
 * Destroy the thread pool
 * if shouldWaitForTasks equal to zero then the thread pool destruction is happen after all the tasks
 * in the queue are terminated.
 * Otherwise the destruction happen just after all the threads finished their current execution.
 */
void tpDestroy(ThreadPool *threadPool, int shouldWaitForTasks) {
  int i;
  if((pthread_mutex_lock(&threadPool->lock))!=0)
    errorHandler(Lock,threadPool);
  threadPool->beingDestroyed = true;
  //whether to execute all the tasks in the queue or not
  if (shouldWaitForTasks == 0)
    threadPool->canExecute = false;
  else
    threadPool->canExecute = true;
  if((pthread_mutex_unlock(&threadPool->lock))!=0)
    errorHandler(Unlock,threadPool);
  //waking up all the thread that related to cond
  if (pthread_cond_broadcast(&threadPool->cond) != 0)
    errorHandler(Cond_BroadCast,threadPool);
  //wait for all the threads to finish their execution.
  for (i = 0; i < threadPool->numOfThreads; i++) {
    if((pthread_join(threadPool->ntid[i], NULL))!=0)
      errorHandler(Pthread_Join,threadPool);
  }
  //free all the allocation inside the thread pool.
  freeThreadPool(threadPool);
}

/*
 * In this function all the tasks are being excecuted by the availabe thread.
 * each thread executes the tasks in the top of the queue until the thread pool are being destroyed.
 * If the queue is empty then the thread is waiting for new tasks by cond_wait.
 */
void *run(void *threadPoolArg) {
  ThreadPool *threadPool = (ThreadPool *) (threadPoolArg);
  int needToLock = 1;
  while (1) {
    if (needToLock)
      if((pthread_mutex_lock(&threadPool->lock))!=0)
        errorHandler(Lock,threadPool);
    needToLock = 1;
    //if the thread pool are being destroyed and there are no more tasks left.
    if (!threadPool->canExecute || (osIsQueueEmpty(threadPool->queue) && (threadPool->beingDestroyed) == true)) {
      if((pthread_mutex_unlock(&threadPool->lock))!=0)
        errorHandler(Unlock,threadPool);
      break;
    }
    //if the tasks queue is empty then the thread is waiting ,by cond_wait, for signal that
    // will occur by inserting a new tasks to the queue.
    if (osIsQueueEmpty(threadPool->queue)) {
      if((pthread_cond_wait(&threadPool->cond, &threadPool->lock))!=0)
        errorHandler(Cond_Wait,threadPool);
      needToLock = 0;
      continue;
    }
    //run the task that in the top of the queue
    Task *task = osDequeue(threadPool->queue);
    if((pthread_mutex_unlock(&threadPool->lock))!=0)
      errorHandler(Unlock,threadPool);
    if (task != NULL) {
      task->computeFunc(task->param);
      free(task);
    }

  }
  return (void *) (0);
}

//check if the thread pool fields, that are being used in the insertion, aren't null.
bool validateThreadPoolForInsert(ThreadPool *thread_pool) {
  if (thread_pool == NULL)
    return false;
  if((pthread_mutex_lock(&thread_pool->lock))!=0)
    errorHandler(Lock,thread_pool);
  if (thread_pool->queue == NULL) {
    if((pthread_mutex_unlock(&thread_pool->lock))!=0)
      errorHandler(Unlock,thread_pool);
    return false;
  }
  if((pthread_mutex_unlock(&thread_pool->lock))!=0)
    errorHandler(Unlock,thread_pool);
  return true;
}

//free the thread pool fields and destory the mutex and the cond.
void freeThreadPool(ThreadPool *thread_pool) {
  if (thread_pool == NULL)
    return;
  Task *task;
  if (thread_pool->ntid != NULL)
    free(thread_pool->ntid);
  task = osDequeue(thread_pool->queue);
  while (task != NULL) {
    free(task);
    task = osDequeue(thread_pool->queue);
  }
  osDestroyQueue(thread_pool->queue);
  pthread_mutex_destroy(&thread_pool->lock);
  pthread_cond_destroy(&thread_pool->cond);
  free(thread_pool);
}

/*
 * handle all errors.
 * report the error message by perror, and the free the threadpool and
 */
void errorHandler(Error error, ThreadPool *thread_pool) {
  switch (error) {
    case Malloc: {
      perror("Memory allocation error\n");
      break;
    }
    case Mutex_Init: {
      perror("mutex init failed\n");
      break;
    }
    case Cond_Init: {
      perror("cond init failed\n");
      break;
    }
    case Pthread_Create: {
      perror("can't create thread\n");
      break;
    }
    case Pthread_Join:{
      perror("can't join to thread\n");
      break;
    }
    case Lock:{
      perror("can't lock the mutex\n");
      break;
    }
    case Unlock:{
      perror("can't unlock the thread\n");
      break;
    }
    case Cond_Signal:{
      perror("Error occurred while using cond_signal func\n");
      break;
    }
    case Cond_Wait:{
      perror("Error occurred while using cond_wait func\n");
      break;
    }
    case Cond_BroadCast:{
      perror("Error occurred while using cond_broadcast func\n");
      break;
    }
  }
  freeThreadPool(thread_pool);
  exit(-1);
}
