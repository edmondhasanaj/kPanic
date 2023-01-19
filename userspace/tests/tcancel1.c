#include "stdlib.h"
#include "stdio.h"
#include "pthread.h"

#define NUM_THREADS 200

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
    int status = pthread_create(&thread_ids[i], 0, thread_args, &thread_ids[i]);
    int status2 = pthread_cancel(thread_ids[i]);
    printf("Started and canceled thread %ld with status = %d and status = %d\n", thread_ids[i], status, status2);
  }

  void* ret_val = 0;
  int status = pthread_join(thread_ids[NUM_THREADS - 1], &ret_val);
  printf("Joining canceled thread = %ld with ret_val = %ld with status = %d\n", thread_ids[NUM_THREADS - 1], (size_t)ret_val, status);

  status = pthread_cancel(thread_ids[NUM_THREADS - 1]);
  printf("Cancelling the thread = %ld again with status = %d\n", thread_ids[NUM_THREADS - 1], status);

  printf("Exiting main\n");

  exit(1);
}

