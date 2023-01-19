#include <stdio.h>
#include "unistd.h"
#include "wait.h"
#include <assert.h>
#include "../libc/include/pthread.h"

pthread_t thread_ids[16];
void* thread(void* tid)
{
  printf("Received TID %ld\n", *(size_t*)tid);
  for(int i = 0; i < 10; i++)
  {
    printf("I am FREE THREAD TID = %ld AND I DON'T HAVE TO WAIT\n", *(size_t*)tid);
  }

  return 0;
}

void forkFunction() {
  int ret_val = fork();

  if(ret_val)
  {
    pthread_t thread_id;
    printf("[parent proc] WAITING FOR %d TO FINISH\n", ret_val);
    pthread_create(&thread_id, 0, thread, &thread_id);
    pid_t ss = waitpid(ret_val, NULL, 0);
    printf("WAITPID FOR %d RETURNED %ld\n", ret_val, ss);
    assert(ss == ret_val);
    printf("[parent proc] FINALLY\n");
  }
  else{
    printf("[child proc] I'm going to sleep\n");
    sleep(3);
    printf("[child proc] WOKE UP\n");
  }
}

int main() {

  for(size_t counter = 0; counter < 4; counter++) {
    forkFunction();
  }

}