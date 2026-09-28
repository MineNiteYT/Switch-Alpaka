




#include <errno.h>
#include <malloc.h>
#include <unistd.h>

int posix_memalign(void **memptr, size_t alignment, size_t size) {
    void *p = memalign(alignment, size);
    if (!p) {
        return ENOMEM;
    }
    *memptr = p;
    return 0;
}

long sysconf(int name) {
    switch (name) {
        case _SC_NPROCESSORS_ONLN:
        case _SC_NPROCESSORS_CONF:
            return 4;
        case _SC_PAGESIZE:
            return 4096;
        default:
            return -1;
    }
}
