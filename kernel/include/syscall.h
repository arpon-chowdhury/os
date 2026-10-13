#ifndef ON_SYSCALL_H
#define ON_SYSCALL_H
#include <stdint.h>
#include "on/syscall.h"
struct syscall_request { uint64_t number, arguments[6]; };
/* Trusted kernel request/saved-frame pointers, never user pointers. */
int64_t syscall_dispatch(const struct syscall_request *request,const void *frame);
/* Kept for preparation-hook callers; not hardware dispatch. */
int64_t syscall_unavailable(const struct syscall_request *request);
#endif
