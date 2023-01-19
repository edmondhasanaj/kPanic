#include <stdio.h>
#include "sched.h"

#define GLOBAL_SIZE (1024ull * 50ull)
#define LOCAL_SIZE  (1024ull * 8ull)

char global_cow_array[GLOBAL_SIZE];

// aquiring some pages before fork (cow) and then overwriting them
// also writing on shared pages "at the same time"
int main() {
  char local_cow_array[LOCAL_SIZE] = {0};

  // writing the alphabet in uppercase letters
  for (int i = 0; i < GLOBAL_SIZE / 2; ++i) {
    global_cow_array[i] = 65 + (i % 26);
  }

  for (int i = 0; i < LOCAL_SIZE / 2; ++i) {
    local_cow_array[i] = 65 + (i % 26);
  }

  int ret_val = fork();

  if(ret_val) {
    //parent
    for (int i = 0; i < GLOBAL_SIZE / 2; ++i) {
      global_cow_array[(GLOBAL_SIZE / 2) + i] = 65 + (i % 26);
    }

    for (int i = 0; i < LOCAL_SIZE / 2; ++i) {
      local_cow_array[(LOCAL_SIZE / 2) + i] = 65 + (i % 26);
    }
  } else {
    // child
    // writing the alphabet in lowercase letters
    for (int i = 0; i < GLOBAL_SIZE; ++i) {
      global_cow_array[i] = 97 + (i % 26);
    }

    for (int i = 0; i < LOCAL_SIZE; ++i) {
      local_cow_array[i] = 65 + (i % 26);
    }
  }

  printf("last char in local array: %c\n", local_cow_array[LOCAL_SIZE - 1]);

  return 0;
}
