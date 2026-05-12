#ifndef HW_G233_PWM_H
#define HW_G233_PWM_H

#include "hw/core/sysbus.h"
#include "qemu/timer.h"
#include "qom/object.h"
#define TYPE_G233_PWM "riscv.g233.pwm"

typedef struct G233PWMState G233PWMState;

#define BASE_OFFSET_CHN_IDX(CHN, IDX)  (0x10 + (CHN) * 0x10 + (IDX) * 4)
#define PWM_CH_CTRL(CHN)    BASE_OFFSET_CHN_IDX(CHN, 0)
#define PWM_CH_PERIOD(CHN)  BASE_OFFSET_CHN_IDX(CHN, 1)
#define PWM_CH_DUTY(CHN)    BASE_OFFSET_CHN_IDX(CHN, 2)
#define PWM_CH_CNT(CHN)    BASE_OFFSET_CHN_IDX(CHN, 3)

#define PWM_CHN_COUNT 4
#define PWM_GLB 0x00000000
#define PWM_TICK_CNT_PER_SEC 1000000ULL
#define PWM_TICK_NS (1000000000ULL / PWM_TICK_CNT_PER_SEC)     /* qtimer is 1us per tick */

#define CHN_ID(ADDR)  ({                                                  \
    uint32_t _addr = (ADDR);                                            \
    _addr >= 0x10 && _addr < 0x50 ? ((_addr) - 0x10) / 0x10 :          \
                                    PWM_CHN_COUNT;                      \
})

DECLARE_INSTANCE_CHECKER(G233PWMState, G233_PWM, TYPE_G233_PWM);

typedef struct PWMChannel
{
    G233PWMState *parent;
    uint32_t chn_id;
    uint32_t ctrl;
    uint32_t period;
    uint32_t duty;
    uint32_t cnt;
    QEMUTimer timer;
    uint64_t start_ns;
} PWMChannel;

struct G233PWMState
{
    SysBusDevice parent_obj;
    MemoryRegion mmio;

    /* registers */
    // uint32_t regs[PWM_CHN_COUNT * 4 + 4];
    uint32_t REG_PWM_GLB;                           /* Global Register */
    PWMChannel chns[PWM_CHN_COUNT];

    /* Interrupt */
    qemu_irq irq;
};

#endif