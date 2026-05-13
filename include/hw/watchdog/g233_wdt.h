#ifndef HW_G233_WDT_H
#define HW_G233_WDT_H

#include "hw/core/sysbus.h"
#include "qom/object.h"
#include "qemu/timer.h"

#define TYPE_G233_WDT "riscv.g233.wdt"

#define G233_WDT_LOAD 0x0004
#define G233_WDT_VAL  0x0008
#define G233_WDT_KEY  0x0010

#define G233_WDT_CTRL 0x0000
#define G233_WDT_CTRL_EN (1U << 0)
#define G233_WDT_CTRL_INTEN (1U << 1)
#define G233_WDT_CTRL_RSTEN (1U << 2)
#define G233_WDT_CTRL_LOCK (1U << 3)
#define WDT_CTRL_VALID_MASK (G233_WDT_CTRL_EN | G233_WDT_CTRL_INTEN | G233_WDT_CTRL_RSTEN)

#define G233_WDT_SR   0x000C
#define G233_WDT_SR_TIMEOUT (1U << 0)
#define G233_WDT_SR_VALID_MASK (G233_WDT_SR_TIMEOUT)

#define G233_WDT_NREGS 4

#define WDT_TICK_CNT_PER_SEC 1000000ULL    /* 1MHz */
#define WDT_TICK_NS (1000000000ULL / WDT_TICK_CNT_PER_SEC)     /* qtimer is 1us per tick */

typedef struct G233WDTState G233WDTState;

DECLARE_INSTANCE_CHECKER(G233WDTState, G233_WDT, TYPE_G233_WDT);

enum {
    G233_WDT_KEY_FEED = 0x5A5A5A5A,
    G233_WDT_KEY_LOCK = 0x1ACCE551
};

struct G233WDTState
{
    SysBusDevice parent;
    uint32_t regs[G233_WDT_NREGS];
    uint64_t start_ns;
    

    MemoryRegion mmio;
    qemu_irq irq;
    QEMUTimer timer;
};

#endif