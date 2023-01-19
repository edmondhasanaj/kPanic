#include <stdio.h>
#include <unistd.h>

int main()
{
  printf("Testing execv ... \n");
  char* args[] = {"arg1", "arg2", "arg3", "arg4", "arg5", "arg6", "arg7", "arg8", "arg9", NULL};
  execv("/usr/execvme.sweb", args);
  return 0;
}
