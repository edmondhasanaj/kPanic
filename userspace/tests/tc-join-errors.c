#include <stdio.h>
#include "../libc/include/pthread.h"

void* thread(void* params)
{
  for(int i = 0; i < 6; i++)
  {
    printf("I am threading without args rn. My val: %d\n", i);
  }

  return (void*)0;
}

int main()
{
  printf("This test case tries to do joins with problems in user space.\n");
  printf("The kernel should resist! The process should crash! \n");

  printf("------------------------------------------------------\n");
  printf("JOIN WITH NOT_FOUND T_ID\n");
  pthread_t id = 0;
  int sstatus= pthread_create(&id, 0, &thread, 0);
  printf("Started thread %ld with status = %d\n", id, sstatus);

  int jstatus = pthread_join(id + 1, 0x0);
  printf("Joined thread %ld with status %d\n", id + 1, jstatus);

  printf("------------------------------------------------------\n");
  printf("JOIN WITH FAKE RET_VAL KERNEL_SPACE ADDR = 0xffffffffffffffff\n");
  jstatus = pthread_join(id, (void**)0xffffffffffffffff);
  printf("Joined thread %ld with status %d\n", id, jstatus);

  printf("------------------------------------------------------\n");
  printf("JOIN WITH FAKE RET_VAL USER_SPACE ADDR = 0x0123\n");
  jstatus = pthread_join(id, (void**)0x0123);
  printf("Joined thread %ld with status %d\n", id, jstatus);

  printf("Process ending with code 0\n");
  return 0;
}
