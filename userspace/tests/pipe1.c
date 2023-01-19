#include "unistd.h"
#include "stdio.h"
#include <assert.h>
#include "fcntl.h"
int main()
{
    int fd[2];
    // 0 -read
    // 1 - write
    if(pipe(fd) == -1)
    {
        printf("ERROR while opening pipe\n");
        return 1;
    }
    printf("R:%d| W:%d\n", fd[0], fd[1]);
    int f = fork();
    if(f == 0)
    {
        close(fd[0]);
        char *s = "ABCD";
        printf("PARENT READS %ld\n", write(fd[1], &s, 4));
        printf("CLOSING CHILD\n");
        sleep(1);
    }
    else
    {
        sleep(2);
        close(fd[1]);
        char *r = "OOOO";
        printf("PARENT READS %ld\n", read(fd[0], &r, 4));
        printf("CHILD SENT: |%s|\n", r);
        r = "OOOO";
        printf("PARENT READS %ld\n", read(fd[0], &r, 4));
        printf("CHILD SENT: |%s|\n", r);
    }
}
