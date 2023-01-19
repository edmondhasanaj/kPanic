#include "stdio.h"
#define NUM_PAGES   10
#define PAGE_SIZE 4096

int main()
{
  printf("NUM_PAGES: %d | Create more page faults than allowed (10) ...\n", NUM_PAGES);

  char str[PAGE_SIZE * NUM_PAGES];
  str[PAGE_SIZE * NUM_PAGES - 1] = 'X';
  printf("%c", str[PAGE_SIZE * NUM_PAGES - 1]);

  return 0;
}
