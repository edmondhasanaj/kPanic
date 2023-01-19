#include <stdio.h>
#include <unistd.h>

int main()
{
  printf("None existing file ...\n");
  char* args[] = {"arg1", "arg2", "arg3", NULL};
  execv("/usr/none_existing_file.sweb", args);
  return 0;
}
