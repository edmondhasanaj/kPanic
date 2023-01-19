/**
 * testcase for exit
 *
 * calling exit from a (pseudo)random thread that is created by the first thread
 * exit can be called only once
 */


#include "stdlib.h"
#include "stdio.h"
#include "pthread.h"
#include "rand.h"

#define NUM_THREADS 200

int called_exit = 0;

void* thread_args(void* tid)
{
  printf("Received TID %ld\n", *(size_t*)tid);
  for(int i = 0; i < 10; i++)
  {
    printf("I am TID = %ld with args rn. My val: %d\n", *(size_t*)tid, i);
  }
  if(rand() % 5 == 0 && called_exit == 0) {
    called_exit = 1;
    printf("\n\n\n------------------EXIT by thread %ld------------------\n\n\n", *(size_t*)tid);
    exit(69);
  }

  return 0;
}

pthread_t thread_ids[NUM_THREADS];


int main() {

  for (int i = 0; i < NUM_THREADS; ++i)
  {
//    thread_ids[i] = i + 1;
    int status = pthread_create(&thread_ids[i], 0, thread_args, &thread_ids[i]);
    printf("Started thread %ld with status = %d\n", thread_ids[i], status);
  }

//  exit(1);

  printf("reached end of main() -> exit (probably!!!) called from main()\n");
}

