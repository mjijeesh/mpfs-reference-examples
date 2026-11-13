#ifndef LWIP_ARCH_CC_H
#define LWIP_ARCH_CC_H

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stddef.h>
#include <limits.h>
#include <sys/types.h>
#include <sys/time.h>



#ifndef SSIZE_MAX
#define SSIZE_MAX               LONG_MAX
#endif

#ifndef BYTE_ORDER
#define BYTE_ORDER              LITTLE_ENDIAN
#endif

#define PACK_STRUCT_FIELD(x)    x
#define PACK_STRUCT_STRUCT      __attribute__((packed))
#define PACK_STRUCT_BEGIN
#define PACK_STRUCT_END

#define LWIP_PLATFORM_DIAG(x)   do { printf x; } while (0)
#define LWIP_PLATFORM_ASSERT(x) do { (void)(x); while (1) { __asm__ volatile("wfi"); } } while (0)

#define LWIP_RAND()             ((uint32_t)rand())

#endif /* LWIP_ARCH_CC_H */