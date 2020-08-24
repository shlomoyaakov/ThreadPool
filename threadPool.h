//Shlomo Yakov 206322588
#ifndef __THREAD_POOL__
#define __THREAD_POOL__
#include <stdio.h>
#include <pthread.h>
#include "stdlib.h"
#include <stdbool.h>
#include "osqueue.h"

typedef struct thread_pool {
  int numOfThreads;
  pthread_t* ntid;
  OSQueue* queue;
  pthread_mutex_t lock;
  pthread_cond_t cond;
  bool beingDestroyed;
  bool canExecute;
} ThreadPool;

typedef struct task {
  void (*computeFunc)(void *);
  void *param;
} Task;

typedef enum {
  Malloc = 0,
  Mutex_Init,
  Cond_Init,
  Pthread_Create,
  Pthread_Join,
  Lock,
  Unlock,
  Cond_BroadCast,
  Cond_Signal,
  Cond_Wait
} Error;

ThreadPool *tpCreate(int numOfThreads);

void tpDestroy(ThreadPool *threadPool, int shouldWaitForTasks);

int tpInsertTask(ThreadPool *threadPool, void (*computeFunc)(void *), void *param);

#endif