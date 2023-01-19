#include "unistd.h"
#include "stdio.h"
#include "../libc/include/pthread.h"
#include <assert.h>

#define NUM_THREADS (int)10e1
#define NUM_ITERATIONS (int)10e1

void* thread_to_sleep(void* tid)
{
  int acc = 0;
  for(int i = 0; i < NUM_ITERATIONS/2; i++)
  {
    printf("A thread %ld is doing something\n", *(size_t*)tid);
    acc++;
  }
  printf("TID %ld is put to sleep\n", *(size_t*)tid);
  sleep(2);
  printf("TID %ld wakes up\n", *(size_t*)tid);
  for(int i = 0; i < NUM_ITERATIONS/2; i++)
  {
    printf("A thread %ld continues doing something\n", *(size_t*)tid);
    acc++;
  }
  assert(acc == NUM_ITERATIONS);
  return 0;
}

pthread_t thread_ids[NUM_THREADS];

int main()
{
    for (int i = 0; i < NUM_THREADS; ++i)
    {
      int status = pthread_create(&thread_ids[i], 0, thread_to_sleep, &thread_ids[i]);
      printf("Started thread %ld with status = %d\n", thread_ids[i], status);
    }
    for (int i = 0; i < NUM_THREADS; ++i)
    {
      pthread_join(thread_ids[i], NULL);
      //printf("Joined thread %ld with status = %d\n", thread_ids[i], status);
    }
}
