#pragma once
// Host generator only: upstream uses four GCC printf-check annotations.
// MSVC lacks this syntax. This does not change guest instruction translation.
#if defined(_MSC_VER) && !defined(__clang__)
#define __attribute__(annotation)
#endif
