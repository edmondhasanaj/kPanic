/**
 * testing 3 and more processes sharing the same page
 *
 * searching in the terminal (ctrl + shift + f) should find that line:
 *     [COWMANAGER ][COW] case 3: 3 process left in the set
 *                                ^
 *                   that three could be any number >= 3
 *
 *  This means that there were 3 or more processes in a certain ppn in
 *  COWManager::cow_map_, which means that 3 or more processes shared the same ppn
 */

#include <stdio.h>
#include "sched.h"

#define SLEEP_TIME 2
#define NUM_FORKS 5
#define ARRAY_SIZE (1024ull * 16)

char array[ARRAY_SIZE];

// simply iterate over the array to write to pages
void iterate(int small) {
  int offset = 0;

  // determines whether the alphabet is lower of uppercase
  if(small)
    offset = 97;
  else
    offset = 65;

  for (int counter = 0; counter < ARRAY_SIZE; counter++) {
    array[counter] = offset + (counter % 26);
  }
}

// fork new processes and let them do stuff to write to pages
int forkFunction() {
  int ret_val = fork();

  if(ret_val)
    printf("[parent proc] ret_val: %d\n", ret_val);
  else {
    printf("[child  proc] ret_val: %d -> starting to sleep\n", ret_val);
    sleep(SLEEP_TIME);
    printf("[child  proc] returning from sleep -> iterating\n");
    iterate(0);
    printf("[child  proc] back to sleep\n");
    sleep(SLEEP_TIME);
  }

  return ret_val;
}

int main() {

  iterate(1);

  for (int counter = 0; counter < NUM_FORKS; ++counter) {
    forkFunction();
  }

  printf("%lluth elem in array: %c\n", (ARRAY_SIZE / 2), array[ARRAY_SIZE / 2]);
  return 0;
}