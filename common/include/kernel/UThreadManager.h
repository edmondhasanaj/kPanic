#pragma once
#include "types.h"
#include "UserThread.h"

typedef size_t pthread_t;
typedef unsigned int pthread_attr_t;
typedef struct
{
  void *(*start_routine)(void *);
  void *args;
} thread_args;

/**
 * Manages the user threads
 */
class UThreadManager
{
  public:
    static UThreadManager* instance();

    size_t create_thread(pthread_t* thread,
                         const pthread_attr_t* attr,
                         void (*libc_exec)(void*(*start_routine)(void*), void*),
                         void*(*start_routine)(void*),
                         void* arg);

    void exit_thread(void* retval);
    size_t join_thread(pthread_t id, void** retval);
    size_t cancel_thread(pthread_t id);

  private:
    UThreadManager();
    static UThreadManager* instance_;
};