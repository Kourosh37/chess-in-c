#ifndef THREADING_H
#define THREADING_H

#include <stdbool.h>

typedef void* (*ChessThreadStart)(void* arg);

typedef struct ChessThread {
    void* handle;
    bool active;
} ChessThread;

bool chess_thread_create(ChessThread* thread, ChessThreadStart start, void* arg);
void chess_thread_join(ChessThread* thread);

#endif
