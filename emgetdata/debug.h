#ifndef DEBUG_H
#define DEBUG_H

#include <stdio.h>

// デバッグモードであれば1、そうでなければ0を設定します。
#ifndef DEBUG_MODE
#define DEBUG_MODE 0
#endif

#if DEBUG_MODE
#define DEBUG_PRINT(...) fprintf(stderr, __VA_ARGS__)
#else
#define DEBUG_PRINT(...) ((void)0)
#endif

#endif // DEBUG_H
