#include "stdio.h"
#include "time.h"
#include "../libc/include/pthread.h"

#define NUM_THREADS (int)10e1
#define NUM_ITERATIONS (int)10e1


void* thread_args(void* tid)
{
  clock_t before = clock();
  for(int i = 0; i < NUM_ITERATIONS; i++)
  {
    clock_t dummy = clock();
    dummy -= before;
  }
  clock_t after = clock();
  printf("A thread %ld with one loop of %d clock() calls consumes %2.7fs of CPU time\n", *(size_t*)tid ,NUM_ITERATIONS, (after - before)/((float)CLOCKS_PER_SEC));
  return 0;
}

pthread_t thread_ids[NUM_THREADS];

int main()
{
    clock_t before = clock();
    printf("Before: %d\n", before);
    for (int i = 0; i < NUM_THREADS; ++i)
    {
      int status = pthread_create(&thread_ids[i], 0, thread_args, &thread_ids[i]);
      printf("Started thread %ld with status = %d\n", thread_ids[i], status);
    }
    for (int i = 0; i < NUM_THREADS; ++i)
    {
      pthread_join(thread_ids[i], NULL);
      //printf("Joined thread %ld with status = %d\n", thread_ids[i], status);
    }

    clock_t after = clock();
    printf("After : %d\n", after);
    printf("A process with %d threads consumed %2.7f s of CPU time\n", NUM_THREADS, (after - before)/((float)CLOCKS_PER_SEC));
}
