#include "stdio.h"
#include "pthread.h"

#define NUM_PAGES    5
#define PAGE_SIZE 4096

void* fn();
char* global_ptr = NULL;

int main()
{
  printf("NUM_PAGES: %d | Create page fault in another thread ...\n", NUM_PAGES);

  pthread_t thread_id;
  int status = pthread_create(&thread_id, NULL, &fn, NULL);
  printf("Started thread %ld with status = %d\n", thread_id, status);

  while (global_ptr == NULL);  // busy loop
    //pthread_yield();

  global_ptr[PAGE_SIZE * NUM_PAGES - 1] = 'X';
  printf("%c", global_ptr[PAGE_SIZE * NUM_PAGES - 1]);

  return 0;
}

void* fn()
{
  char str[PAGE_SIZE * NUM_PAGES];
  global_ptr = str;
  return NULL;
}
