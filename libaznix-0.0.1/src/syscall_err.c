/* XXX Copied from uClibc syscall wrappers and modified */
#include <errno.h>

#undef __set_errno
#define __set_errno(X) {(errno) = (X);}

/* Store the %rax into errno of the calling thread */
int __az_syscall_error(void)
{
    register int err_no __asm__ ("%rcx");
    __asm__ ("mov %rax, %rcx\n\t"
            "neg %rcx");
    __set_errno(err_no);
    return -1;
}
