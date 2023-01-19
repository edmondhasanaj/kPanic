#include <stdio.h>
#include "../libc/include/pthread.h"

void* thread_args(void* tid)
{
  printf("Received TID %ld\n", *(size_t*)tid);
  for(int i = 0; i < 10; i++)
  {
    printf("I am TID = %ld with args rn. My val: %d\n", *(size_t*)tid, i);
  }

  return 0;
}

size_t thread_ids[200];

int main()
{
  pthread_t id = 0;
  for (int i = 0; i < 200; ++i)
  {
    thread_ids[i] = i + 1;
    int status = pthread_create(&id, 0, thread_args, &thread_ids[i]);
    printf("Started thread %ld with status = %d\n", id, status);
  }

  //simply wait for other thread xD
  for(int i = 0; i < 10000000; i++)
  {
    i += 10;
    i -= 10;
  }

  printf("Process ending with code 0\n");
  return 0;
}
