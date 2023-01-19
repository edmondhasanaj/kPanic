#include <stdio.h>
#include <unistd.h>

int main()
{
  printf("args is missing NULL at the end ... \n");
  char* args[] = {"arg1", "arg2", "arg3"};
  execv("/usr/execvme.sweb", args);
  return 0;
}
