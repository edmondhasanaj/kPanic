#include <stdio.h>
#include "unistd.h"
#include "wait.h"
#include "assert.h"


int main() {
 int pid_2 = fork();
 if(pid_2)
 {
    assert(pid_2 == 2);
    assert(waitpid(1, NULL, WEXITED) == -1);
    assert(waitpid(-1, NULL, WEXITED) == pid_2);
 }
 else
 {
    sleep(5);
 }
}