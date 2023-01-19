#include <stdio.h>
#include "../libc/include/pthread.h"
#define COUNT 30

pthread_t pid_a[COUNT];
int finished = 0;

void* thread_d(void* args)
{
  pthread_t id = *(pthread_t*)args;
  printf("[D%ld] Started thread D. \n", id);
  printf("[D%ld] Joining with thread A now...\n", id);
  int jStatus = pthread_join(id, args);
  printf("[D%ld] Joined thread A with status = %d\n", id, jStatus);
  printf("[D%ld] End with ret_val = 0\n", id);
  finished -= jStatus;

  if(jStatus != -1)
  {
    printf("DIDN'T FINISH WITH STATUS = -1\n");
    _exit(-1);
  }
  printf("Progress: %d\n", finished);

  return 0;
}

void* thread_c(void* args)
{
  pthread_t id = *(pthread_t*)args;
  printf("[C%ld] Started thread C. Starting thread D and waiting for it...\n", id);
  pthread_t tid = 0;
  int cStatus = pthread_create(&tid, 0x0, thread_d, args);
  printf("[C%ld] Created thread D with status = %d. Joining with it now...\n", id, cStatus);
  int jStatus = pthread_join(tid, args);
  printf("[C%ld] Joined thread D with status = %d\n", id, jStatus);
  printf("[C%ld] End with ret_val = 0\n", id);
  return 0;
}

void* thread_b(void* args)
{
  pthread_t id = *(pthread_t*)args;
  printf("[B%ld] Started thread B. Starting thread C and waiting for it...\n", id);
  pthread_t tid = 0;
  int cStatus = pthread_create(&tid, 0x0, thread_c, args);
  printf("[B%ld] Created thread C with status = %d. Joining with it now...\n", id, cStatus);
  int jStatus = pthread_join(tid, args);
  printf("[B%ld] Joined thread C with status = %d\n", id, jStatus);
  printf("[B%ld] End with ret_val = 0\n", id);
  return 0;
}

void* thread_a(void* args)
{
  pthread_t id = *(pthread_t*)args;
  printf("[A%ld] Started thread A. Starting thread B and waiting for it...\n", id);
  pthread_t tid = 0;
  int cStatus = pthread_create(&tid, 0x0, thread_b, args);
  printf("[A%ld] Created thread B with status = %d. Joining with it now...\n", id, cStatus);
  int jStatus = pthread_join(tid, 0x0);
  printf("[A%ld] Joined thread B with status = %d\n", id, jStatus);
  printf("[A%ld] End with ret_val = 0\n", id);
  finished++;
  printf("Progress: %d\n", finished);
  return 0;
}

int main()
{
  printf("This test case tries to do joins 4 threads so that a deadlock is produced. \n");
  printf("The kernel should resist! \n");

  for(int i = 0; i < COUNT; i++)
  {
    int cStatus = pthread_create(&pid_a[i], 0, thread_a, (void*)&pid_a[i]);
    printf("[MAIN] The thread A[%ld] is created with status code = %d\n", pid_a[i], cStatus);
  }

  while(finished < 2 * COUNT)
  {
    printf("Progress: %d\n", finished);
  }

  printf("Process ending with code 0\n");

  return 0;
}
