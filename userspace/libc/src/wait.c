#include "wait.h"
#include "sys/syscall.h"
#include "../../../common/include/kernel/syscall-definitions.h"

/**
 * function stub
 * posix compatible signature - do not change the signature!
 */
pid_t waitpid(pid_t pid, int *status, int options)
{
  return __syscall(sc_waitpid, (size_t) pid, (size_t) status, (size_t) options, 0x00, 0x00);
}


