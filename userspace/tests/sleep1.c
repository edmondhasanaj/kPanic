#include "unistd.h"
#include "stdio.h"
#include <assert.h>

int main()
{
    size_t seconds = 2;
    printf("I'll sleep for %ld seconds\n", seconds);
    size_t s = sleep(seconds);
    assert(s == 0);
    printf("Good Morning!\n");
}
