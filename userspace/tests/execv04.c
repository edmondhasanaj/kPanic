#include <stdio.h>
#include <unistd.h>

int main()
{
  printf("Last arg is an empty string (\"\")\n");
  char* args[] = {"arg1", ""};
  execv("/usr/execvme.sweb", args);
  return 0;
}
