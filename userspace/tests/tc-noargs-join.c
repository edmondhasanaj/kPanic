#include <stdio.h>
#include "../libc/include/pthread.h"

void* thread_no_arg(void* params)
{
  for(int i = 0; i < 6; i++)
  {
    printf("I am threading without args rn. My val: %d\n", i);
  }

  return (void*)10;
}

int main()
{
  pthread_t id = 0;
  for (int i = 1; i <= 10; ++i)
  {
    printf("Started thread %d with status = %d\n", i, pthread_create(&id, 0, &thread_no_arg, 0));

    void* ret_val = 0;
    int stat = pthread_join(id, &ret_val);
    printf("Joined thread %ld with return value = %p with status = %d\n", id, ret_val, stat);
  }

  printf("Process ending with code 0");
  return 0;
}
