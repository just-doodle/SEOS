#ifndef __SYSTEM_H__
#define __SYSTEM_H__

#include "stdarg.h"
#include "stdbool.h"


#ifdef __USE_INBUILT_STDINT__
typedef unsigned char uint8_t;
typedef unsigned short uint16_t;
typedef unsigned int uint32_t;
typedef unsigned long uint64_t;

typedef signed char int8_t;
typedef signed short int16_t;
typedef signed int int32_t;
typedef signed long int64_t;

#else
#include "stdint.h"
#endif

typedef uint32_t size_t;


#define khalt asm("cli"); \ 
            asm("hlt")

#define KERNEL_START 0x100000
#define KERNEL_END  k_end

#define NULL (void*)0
/*
* version system
* REVN.YY.MM.RELN(STATUS)
* REVN: revision number (increases when major release)
* YY: first digit of year of release (eg. if year is 2023 then YY=23)
* MM: month of release
* RELN: index of current release in the current month
* STATUS: [PR]:Prerelease, [AL]:alpha, [NR]:Normal release
*/
#define KERNEL_VERSION "1.23.03.6NR"

#define KB 1024
#define MB (1024*KB)
#define GB (1024*MB)

extern uint32_t k_end;

void reboot();

typedef struct registers
{
    uint32_t ds;
    uint32_t edi, esi, ebp, esp, ebx, edx, ecx, eax;
    uint32_t ino, ecode;
    uint32_t eip, cs, eflags, useresp, ss;
} registers_t;

#endif