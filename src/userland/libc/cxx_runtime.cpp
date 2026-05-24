extern "C" {
#include "stdlib.h"
#include "stdio.h"
#include "syscall.h"
}

#include <stddef.h>
#include <stdint.h>

namespace std {
struct nothrow_t {
    explicit nothrow_t() = default;
};

const nothrow_t nothrow;
}

extern "C" {
void *__dso_handle = &__dso_handle;
}

struct ExitHandler {
    void (*plain)(void);
    void (*func)(void *);
    void *arg;
    void *dso;
    bool used;
};

static ExitHandler exit_handlers[128];
static size_t exit_handler_count = 0;

static void *alloc_or_abort(size_t size)
{
    if (size == 0) size = 1;
    void *ptr = malloc(size);
    if (ptr == 0) {
        syscall2(8, 0, (uint64_t)"[cxx] operator new allocation failed\n");
        abort();
    }
    return ptr;
}

void *operator new(size_t size)
{
    return alloc_or_abort(size);
}

void *operator new[](size_t size)
{
    return alloc_or_abort(size);
}

void *operator new(size_t size, const std::nothrow_t &) noexcept
{
    if (size == 0) size = 1;
    return malloc(size);
}

void *operator new[](size_t size, const std::nothrow_t &) noexcept
{
    if (size == 0) size = 1;
    return malloc(size);
}

void operator delete(void *ptr) noexcept
{
    free(ptr);
}

void operator delete[](void *ptr) noexcept
{
    free(ptr);
}

void operator delete(void *ptr, size_t) noexcept
{
    free(ptr);
}

void operator delete[](void *ptr, size_t) noexcept
{
    free(ptr);
}

void operator delete(void *ptr, const std::nothrow_t &) noexcept
{
    free(ptr);
}

void operator delete[](void *ptr, const std::nothrow_t &) noexcept
{
    free(ptr);
}

extern "C" int atexit(void (*func)(void))
{
    if (func == 0 || exit_handler_count >= sizeof(exit_handlers) / sizeof(exit_handlers[0])) return -1;

    ExitHandler *handler = &exit_handlers[exit_handler_count++];
    handler->plain = func;
    handler->func = 0;
    handler->arg = 0;
    handler->dso = 0;
    handler->used = true;
    return 0;
}

extern "C" int __cxa_atexit(void (*func)(void *), void *arg, void *dso_handle)
{
    if (func == 0 || exit_handler_count >= sizeof(exit_handlers) / sizeof(exit_handlers[0])) return -1;

    ExitHandler *handler = &exit_handlers[exit_handler_count++];
    handler->plain = 0;
    handler->func = func;
    handler->arg = arg;
    handler->dso = dso_handle;
    handler->used = true;
    return 0;
}

extern "C" void __cxa_finalize(void *dso_handle)
{
    for (size_t i = exit_handler_count; i > 0; i--) {
        ExitHandler *handler = &exit_handlers[i - 1];
        if (!handler->used) continue;
        if (dso_handle != 0 && handler->dso != dso_handle) continue;

        handler->used = false;
        if (handler->func != 0) {
            handler->func(handler->arg);
        } else if (handler->plain != 0) {
            handler->plain();
        }
    }
}

extern "C" void __cxa_pure_virtual(void)
{
    printf("pure virtual function call\n");
    abort();
}

extern "C" void __cxa_deleted_virtual(void)
{
    printf("deleted virtual function call\n");
    abort();
}

extern "C" void __cxa_throw_bad_array_new_length(void)
{
    printf("bad array new length\n");
    abort();
}

extern "C" int __cxa_guard_acquire(uint64_t *guard)
{
    return (*guard & 1) == 0;
}

extern "C" void __cxa_guard_release(uint64_t *guard)
{
    *guard |= 1;
}

extern "C" void __cxa_guard_abort(uint64_t *)
{
}
