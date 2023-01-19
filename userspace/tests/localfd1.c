#include "unistd.h"
#include "stdio.h"
#include <assert.h>
#include "fcntl.h"
int main()
{
    int fd_0 = open("hm", O_CREAT);
    int fd_1 = open("hm", O_RDONLY);
    int ret_val = fork();
    
    if(ret_val)
    {
        
        
        int fd_2 = open("hm", O_WRONLY);
        char* buf = "saddness";
        char* readbuf = "";
        printf("Trying to write in rdonly fd: %ld\n", write(fd_1, buf, 2));
        printf("Trying to read from wronly fd: %ld\n", read(fd_2, buf, 2));
        printf("_______________________\n");
        printf("Trying to write in wronly fd: %ld\n", write(fd_2, buf, 3));
        printf("Trying to read from rdonly fd: %ld\n", read(fd_1, readbuf, 4));
        
        printf("READ: %s\n", readbuf);
        printf("Following FDs should be incrementing\n");
        printf("Original process opened FDs: %d-%d\n", fd_0, fd_1);
        close(fd_0);
        close(fd_1);
        close(fd_2);
    }
    else 
    {
        int fd_3 = open("dd", O_CREAT);
        int fd_4 = open("dd", O_WRONLY);
        printf("   Child process opened FDs: %d-%d\n", fd_3, fd_4);
        close(fd_3);
        close(fd_4);
    }
    return ret_val;
}
