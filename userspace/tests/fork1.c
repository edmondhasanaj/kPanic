#include <stdio.h>

// simple testcase for fork()
int main() {
  int ret_val = fork();

  if(ret_val)
    printf("[parent process] ret_val from fork: %d\n", ret_val);
  else {
    printf("[child  process] ret_val from fork: %d\n", ret_val);
  }
  return ret_val;
}