#include <stdio.h>
#include "../libc/include/pthread.h"

void* thread_no_arg(void* params)
{
  for(int i = 0; i < 1; i++)
  {
    printf("I am threading without args rn. My val: %d\n", i);
  }

  return 0;
}

int main()
{
  pthread_t id = 0;
  for (int i = 0; i < 1; ++i)
  {
    printf("Started thread %d with status = %d\n", i, pthread_create(&id, 0, &thread_no_arg, 0));
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
