#include "stdio.h"
#define NUM_PAGES    9    // number of pages = number of page faults
#define PAGE_SIZE 4096    // 4kiB

int main()
{
  printf("NUM_PAGES: %d | Create a simple page fault ...\n", NUM_PAGES);

  char str[PAGE_SIZE * NUM_PAGES];
  str[PAGE_SIZE * NUM_PAGES - 1] = 'X';
  printf("%c", str[PAGE_SIZE * NUM_PAGES - 1]);

  return 0;
}
// LINUX: STACK_MAX_SIZE/PAGE_SIZE = 8MiB/4kiB = (8*1024*1024)/(4*1024) = 2048 pages = 2047 further valid page faults
// SWEB:  STACK_MAX_SIZE/PAGE_SIZE = 40kiB/4kiB = 10 pages = 9 further valid page faults
