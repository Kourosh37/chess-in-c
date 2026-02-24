#include "threading.h"

#include <stdlib.h>
#include <stdint.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <process.h>

typedef struct ThreadStartCtx {
    ChessThreadStart start;
    void* arg;
} ThreadStartCtx;

static unsigned __stdcall chess_thread_entry(void* raw) {
    ThreadStartCtx* ctx = (ThreadStartCtx*)raw;
    ChessThreadStart start = NULL;
    void* arg = NULL;

    if (ctx != NULL) {
        start = ctx->start;
        arg = ctx->arg;
        free(ctx);
    }

    if (start != NULL) {
        (void)start(arg);
    }
    return 0U;
}

bool chess_thread_create(ChessThread* thread, ChessThreadStart start, void* arg) {
    ThreadStartCtx* ctx;
    uintptr_t raw_handle;

    if (thread == NULL || start == NULL || thread->active) {
        return false;
    }

    ctx = (ThreadStartCtx*)malloc(sizeof(*ctx));
    if (ctx == NULL) {
        return false;
    }
    ctx->start = start;
    ctx->arg = arg;

    raw_handle = _beginthreadex(NULL, 0U, chess_thread_entry, ctx, 0U, NULL);
    if (raw_handle == 0U) {
        free(ctx);
        return false;
    }

    thread->handle = (void*)raw_handle;
    thread->active = true;
    return true;
}

void chess_thread_join(ChessThread* thread) {
    HANDLE handle;

    if (thread == NULL || !thread->active || thread->handle == NULL) {
        return;
    }

    handle = (HANDLE)thread->handle;
    WaitForSingleObject(handle, INFINITE);
    CloseHandle(handle);
    thread->handle = NULL;
    thread->active = false;
}

#else

#include <pthread.h>

bool chess_thread_create(ChessThread* thread, ChessThreadStart start, void* arg) {
    pthread_t* handle;

    if (thread == NULL || start == NULL || thread->active) {
        return false;
    }

    handle = (pthread_t*)malloc(sizeof(*handle));
    if (handle == NULL) {
        return false;
    }

    if (pthread_create(handle, NULL, start, arg) != 0) {
        free(handle);
        return false;
    }

    thread->handle = handle;
    thread->active = true;
    return true;
}

void chess_thread_join(ChessThread* thread) {
    pthread_t* handle;

    if (thread == NULL || !thread->active || thread->handle == NULL) {
        return;
    }

    handle = (pthread_t*)thread->handle;
    pthread_join(*handle, NULL);
    free(handle);
    thread->handle = NULL;
    thread->active = false;
}

#endif
