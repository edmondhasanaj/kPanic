#include <stdio.h>
#include <unistd.h>

int main()
{
  printf("None existing path ...\n");
  char* args[] = {"arg1", "arg2", "arg3", NULL};
  execv("/path/to/nowhere/none_existing_file.sweb", args);
  return 0;
}
