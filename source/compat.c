// compat.c - POSIX shims missing from newlib on the Switch (devkitA64).
// Needed by llama.cpp/ggml (llama-mmap.cpp, ggml.c, ggml-cpu.cpp).
// Copied verbatim from the switch-llm-console project, where it's already
// proven to link and run.

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
            return 4; // Tegra X1: 4x Cortex-A57
        case _SC_PAGESIZE:
            return 4096;
        default:
            return -1;
    }
}
