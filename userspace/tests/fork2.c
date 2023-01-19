#include <stdio.h>
#include "../libc/include/pthread.h"

void forkFunction() {
  int ret_val = fork();

  if(ret_val)
    printf("[parent proc] ret_val: %d\n", ret_val);
  else
    printf("[child  proc] ret_val: %d\n", ret_val);
}

// testing fork a little bit more
int main() {

  for(size_t counter = 0; counter < 5; counter++) {
    forkFunction();
  }

}