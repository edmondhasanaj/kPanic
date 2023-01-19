#include "time.h"
#include "stdio.h"

#define NUM_ITERATIONS (int)10e3

int main()
{
    clock_t before = clock();
    printf("Before: %d\n", before);
    for (size_t i = 0; i < NUM_ITERATIONS; i++){}
    clock_t after = clock();
    printf("After : %d\n", after);
    printf("An empty for loop with %d iterations consumes %2.7fs of CPU time\n", NUM_ITERATIONS, (after - before)/((float)CLOCKS_PER_SEC));
}
