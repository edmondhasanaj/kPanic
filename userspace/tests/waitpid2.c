#include <stdio.h>
#include "unistd.h"
#include "wait.h"
#include <assert.h>
#include "../libc/include/pthread.h"


/* Example cyclic deadlock scenario
2 : [3,4]  PID 2 waits for PIDs 3 and 4
3 : [6]
4 : [5]    PID 4 waits for PID 5
5 : [2] <-------- *In PID 5 we try to wait for PID 2 which results in a cyclic deadlock*
6 : []
*/

void* wait_for_pid(void* pid)
{
  printf("I AM PID %d | WAITING FOR %d \n", 2, 3);
  waitpid(3, NULL, WEXITED);
  return 0;
}

// WORKS ONLY IF USING FIRST TIME (Not all processes know each other's id e.g. pid 4 can't wait for pid 5)

int main() {
 int pid_1 = 0;
 int pid_2 = 0;
 int pid_3 = 0;
 int pid_4 = 0;
 int pid_5 = 0;

 pid_1 = fork();
 if(pid_1)
 {
    pid_2 = fork();
    if(pid_2)
    {
      pid_3 = fork();
      if(pid_3)
      {
        pid_4 = fork();
        if(pid_4)
        {
          pid_5 = fork();
          if(pid_5)
          {
            // ORIGINAL PROC (PID1)
            sleep(15); // Doing something
          }
          else
          {
            //I'm PID 6
            sleep(10); // Doing something
          }
        }
        else
        {
          //I'm PID 5
          sleep(7); // Doing something
          printf("**I AM PID %d | WAITING FOR %d **\n", 5, 2);
          assert(waitpid(2, NULL, WEXITED) == -1);
        }
      }
      else
      {
        //I'm PID 4
        sleep(4); // Doing something
        printf("I AM PID %d | WAITING FOR %d \n", 4, 5);
        waitpid(5, NULL, WEXITED);
      }
    }
    else
    {
      //I'm PID 3
      sleep(2); // Doing something
      printf("I AM PID %d | WAITING FOR %d \n", 3, 6);
      waitpid(6, NULL, WEXITED);
    }
 }
 else
 {
   pthread_t thread_id;
   sleep(1);
   //I'm PID 2
   pthread_create(&thread_id, 0, wait_for_pid, &pid_2);
   printf("I AM PID %d | WAITING FOR %d \n", 2, 4);
   waitpid(4, NULL, WEXITED);
 }
}