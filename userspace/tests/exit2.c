#include "stdlib.h"
#include "stdio.h"
#include "pthread.h"

#define NUM_THREADS 6

void* thread_args(void* tid)
{
  printf("Received TID %ld\n", *(size_t*)tid);
  for(int i = 0; i < 10; i++)
  {
    printf("I am TID = %ld with args rn. My val: %d\n", *(size_t*)tid, i);
  }

  return 0;
}

pthread_t thread_ids[NUM_THREADS];

// advanced testcase for exit()
int main() {

  for (int i = 0; i < NUM_THREADS; ++i)
  {
//    thread_ids[i] = i + 1;
    int status = pthread_create(&thread_ids[i], 0, thread_args, &thread_ids[i]);
    printf("Started thread %ld with status = %d\n", thread_ids[i], status);
  }

  printf("Exiting...\n");
  exit(1);
}

