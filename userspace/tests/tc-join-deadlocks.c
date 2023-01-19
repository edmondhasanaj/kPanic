#include <stdio.h>
#include "../libc/include/pthread.h"

pthread_t pid_a;
pthread_t pid_b;
int finished = 0;


void* thread_a(void* args)
{
  printf("Started thread A. Waiting for thread B=%ld to finish\n", pid_b);
  int status = pthread_join(pid_b, 0x0);
  printf("Joined thread B with status = %d\n", status);
  finished++;
  return 0;
}

void* thread_b(void* args)
{
  printf("Started thread B. Waiting for thread A=%ld to finish\n", pid_a);
  int status = pthread_join(pid_a, 0x0);
  printf("Joined thread A with status = %d\n", status);
  finished++;
  return 0;
}

void* dummy_thread(void* args)
{
  pthread_t my_id = *(size_t*)args;
  printf("I have TID = %ld and am joining to myself, because I am dumb\n", my_id);
  int status = pthread_join(my_id, 0x0);
  printf("I finished joining to myself with status: %d\n", status);

  return (void*)0x0;
}
int main()
{
  finished = 0;

  printf("This test case tries to do joins with deadlocks.\n");
  printf("The kernel should resist! \n");

  printf("------------------------------------------------------\n");
  printf("JOIN THREAD WITH ITSELF\n");
  pthread_t id = 0;
  pthread_create(&id, 0, &dummy_thread, &id);
  printf("Created dummy thread with id = %ld. Waiting for it to finish...\n", id);
  pthread_join(id, 0x0);
  printf("Dummy thread finished. If we reach here, it all went right.\n");

  printf("------------------------------------------------------\n");
  printf("JOIN THREAD A TO B AND B TO A\n");
  pthread_create(&pid_a, 0, &thread_a, 0x0);
  pthread_create(&pid_b, 0, &thread_b, 0x0);
  printf("Created dummy thread A with TID = %ld and B with TID = %ld. Waiting for them to finish...\n", pid_a, pid_b);
  while (finished < 2)
  {

  }

  printf("Process ending with code 0\n");
  return 0;
}
