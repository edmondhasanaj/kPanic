#include <stdio.h>
#include "../libc/include/pthread.h"

pthread_t pid_a;
int finished = 0;

void* thread_d(void* args)
{
  printf("[D] Started thread D. \n");
  printf("[D] Joining with thread A now now...");
  int jStatus = pthread_join(pid_a, 0x0);
  printf("[D] Joined thread A with status = %d\n", jStatus);
  printf("[D] End with ret_val = 0\n");
  return 0;
}

void* thread_c(void* args)
{
  printf("[C] Started thread C. Starting thread D and waiting for it...\n");
  pthread_t tid = 0;
  int cStatus = pthread_create(&tid, 0x0, thread_d, 0x0);
  printf("[C] Created thread D with status = %d. Joining with it now...\n", cStatus);
  int jStatus = pthread_join(tid, 0x0);
  printf("[C] Joined thread D with status = %d\n", jStatus);
  printf("[C] End with ret_val = 0\n");
  return 0;
}

void* thread_b(void* args)
{
  printf("[B] Started thread B. Starting thread C and waiting for it...\n");
  pthread_t tid = 0;
  int cStatus = pthread_create(&tid, 0x0, thread_c, 0x0);
  printf("[B] Created thread C with status = %d. Joining with it now...\n", cStatus);
  int jStatus = pthread_join(tid, 0x0);
  printf("[B] Joined thread C with status = %d\n", jStatus);
  printf("[B] End with ret_val = 0\n");
  return 0;
}

void* thread_a(void* args)
{
  printf("[A] Started thread A. Starting thread B and waiting for it...\n");
  pthread_t tid = 0;
  int cStatus = pthread_create(&tid, 0x0, thread_b, 0x0);
  printf("[A] Created thread B with status = %d. Joining with it now...\n", cStatus);
  int jStatus = pthread_join(tid, 0x0);
  printf("[A] Joined thread B with status = %d\n", jStatus);
  printf("[A] End with ret_val = 0\n");
  finished++;
  return 0;
}

int main()
{
  finished = 0;

  printf("This test case tries to do joins 4 threads so that a deadlock is produced. \n");
  printf("The kernel should resist! \n");

  int cStatus = pthread_create(&pid_a, 0, thread_a, 0x0);
  printf("[MAIN] The thread A is created with status code = %d\n", cStatus);
  printf("[MAIN] Waiting for all threads to finish\n");

  while (finished < 1);

  printf("Process ending with code 0\n");
  return 0;
}
