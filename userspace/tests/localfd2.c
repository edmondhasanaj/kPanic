#include "unistd.h"
#include "stdio.h"
#include <assert.h>
#include "fcntl.h"
int main()
{
    int fd_0 = open("hm", O_CREAT);
    int fd_1 = open("hm", O_RDONLY);
    assert(fd_0 != -1);
    assert(fd_1 != -1);
    int ret_val = fork();

    if(ret_val)
    {
        int fd_2 = open("hm", O_WRONLY);
        assert(fd_2 != -1);
    }
    else 
    {
        int fd_3 = open("dd", O_CREAT);
        int fd_4 = open("dd", O_WRONLY);
        assert(fd_3 != -1);
        assert(fd_4 != -1);
    }
    return ret_val;
}
