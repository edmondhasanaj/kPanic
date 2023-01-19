#include <stdio.h>
#include "../libc/include/pthread.h"

#define NUM_THREADS 200

void* thread_no_arg(void* params)
{
  for(int i = 0; i < 6; i++)
  {
    printf("I am threading without args rn. My val: %d\n", i);
  }

  return (void*)10;
}
pthread_t ids[NUM_THREADS];
int main()
{
  
  for (int i = 0; i < NUM_THREADS; ++i)
  {
    printf("Started thread %d with status = %d\n", i, pthread_create(&ids[i], 0, &thread_no_arg, 0));
  }

  for (int i = 0; i < NUM_THREADS; ++i)
  {
    void* ret_val = 0;
    int stat = pthread_join(ids[i], &ret_val);
    printf("Joined thread %ld with return value = %p with status = %d\n", ids[i], ret_val, stat);
  }



  printf("Process ending with code 0");
  return 0;
}
