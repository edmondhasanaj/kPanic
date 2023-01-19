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
        int g = fork();
        if(g == 0)
        {
          sleep(1);
          close(fd[0]);
          char *s = "EFGHIGKLMN";
          printf("PARENT READS %ld\n", write(fd[1], &s, 11));
          printf("CLOSING CHILD\n");
          sleep(1);
        }
        else
        {
          close(fd[0]);
          char *s = "ABCD";
          printf("PARENT READS %ld\n", write(fd[1], &s, 4));
          printf("CLOSING CHILD\n");
          sleep(1);
        }

    }
    else
    {
        sleep(3);
        char *r = "OOOO";
        printf("PARENT TRIES TO READ FROM WRITE FD %ld\n", read(fd[1], &r, 1026));
        printf("PARENT TRIES TO WRITE TO READ FD %ld\n", write(fd[0], &r, 1026));
        printf("PARENT READS %ld\n", read(fd[0], &r, 4));
        printf("CHILD SENT: |%s|\n", r);
        r = "OOOO";
        printf("PARENT READS %ld\n", read(fd[0], &r, 111));
        printf("CHILD SENT: |%s|\n", r);
        char p[1026];
        printf("PARENT TRIES TO WOVERFLOW %ld\n", write(fd[1], &p, 1026));
        printf("PARENT TRIES TO READ %ld\n", read(fd[0], &p, 1026));
        printf("PARENT TRIES TO READ(empty) %ld\n", read(fd[0], &p, 1026));
        printf("---------------Some R/W----------\n");
        int too_much = 2000;
        printf("PARENT TRIES TO W OVERFLOW(%d): %ld\n", too_much, write(fd[1], &p, too_much));
        printf("PARENT TRIES TO R TOO MUCH(%d): %ld\n", too_much, read(fd[0], &p, too_much));
        printf("PARENT TRIES TO W OVERFLOW(%d): %ld\n", too_much, write(fd[1], &p, too_much));
        printf("PARENT TRIES TO R TOO MUCH(%d): %ld\n", too_much, read(fd[0], &p, too_much));
        printf("PARENT TRIES TO W OVERFLOW(%d): %ld\n", too_much, write(fd[1], &p, too_much));
    }
}
