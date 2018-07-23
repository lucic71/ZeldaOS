/*
 *Copyright (c) 2018 Jie Zheng
 */

#ifndef _IOPORT_H
#define _IOPORT_H
#include <lib/include/types.h>
uint8_t inb(uint16_t portid);
void outb(uint16_t portid, uint8_t val);

static inline void io_wait(void)
{
    asm volatile("jmp 1f;"
        "1:jmp 2f;"
        "2:");
}

#endif