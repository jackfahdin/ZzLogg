/* Linux/glibc-only measurement helper. Never linked into the application.
 * Counts requested bytes, not live memory or mimalloc's separate allocations.
 * Using glibc entry points avoids allocator recursion during dlsym startup.
 */
#include <stddef.h>
#include <stdint.h>

#ifndef __GLIBC__
#error "The optional allocation probe requires glibc"
#endif

extern void* __libc_malloc(size_t);
extern void* __libc_calloc(size_t, size_t);
extern void* __libc_realloc(void*, size_t);
extern void __libc_free(void*);

static _Thread_local int enabled;
static _Thread_local uint64_t calls;
static _Thread_local uint64_t bytes;

void regex_alloc_begin(void) { calls = 0; bytes = 0; enabled = 1; }
void regex_alloc_end(void) { enabled = 0; }
uint64_t regex_alloc_calls(void) { return calls; }
uint64_t regex_alloc_bytes(void) { return bytes; }

static void record(size_t size) {
    if (enabled) { ++calls; bytes += size; }
}
void* malloc(size_t size) { record(size); return __libc_malloc(size); }
void* calloc(size_t count, size_t size) { record(count * size); return __libc_calloc(count, size); }
void* realloc(void* ptr, size_t size) { record(size); return __libc_realloc(ptr, size); }
void free(void* ptr) { __libc_free(ptr); }
