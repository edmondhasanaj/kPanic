#include <stdio.h>
#include "../libc/include/pthread.h"

#define NUM_THREADS 3

pthread_t thread_ids[NUM_THREADS];

void forkFunction() {
  int ret_val = fork();

  if(ret_val)
    printf("[parent proc] ret_val: %d\n", ret_val);
  else
    printf("[child  proc] ret_val: %d\n", ret_val);
}

void* test(void* args) {
  printf("new thread with tid: %lu (supplied as an argument)\n", *(size_t*)args);

  for (int counter = 0; counter < 100; ++counter) {
    printf("counting up -> %d\n", counter);
  }

  return (void*)0xDEAD;
}

// testcase for combining fork() and some pthread syscalls
int main() {

  forkFunction();
  for (int i = 0; i < NUM_THREADS; ++i)
  {
    int status = pthread_create(&thread_ids[i], 0, test, &thread_ids[i]);
    printf("Started thread %ld with status = %d\n", thread_ids[i], status);
  }

  for (int i = 0; i < NUM_THREADS; ++i)
  {
    pthread_cancel(thread_ids[i]);
    pthread_join(thread_ids[i], NULL);
  }

  return 0;
}

