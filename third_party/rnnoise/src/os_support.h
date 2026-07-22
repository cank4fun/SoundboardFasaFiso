#ifndef OS_SUPPORT_H
#define OS_SUPPORT_H

#include <stddef.h>
#include <string.h>

#ifndef OPUS_CLEAR
#define OPUS_CLEAR(destination, count) \
    memset((destination), 0, (size_t)(count) * sizeof(*(destination)))
#endif

#endif /* OS_SUPPORT_H */
