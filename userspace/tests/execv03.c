#include <stdio.h>
#include <unistd.h>

int main()
{
  printf("Uniform initialization with empty String ({} == {\"\"})\n");

  char* args[] = {};
  printf("  args: %14zx\n", (size_t)args);
  printf(" *args: %14zx\n", (size_t)*args);
  printf("**args: '%c'\n", **args);

  execv("/usr/execvme.sweb", args);
  return 0;
}

// char** args == char* args[]
// char** args → char* arg0 → "string0"
//               char* arg1 → "string1"
//               char* …    → "…"
//               char* argN → "stringN"
//               char* NULL
