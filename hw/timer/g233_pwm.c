/*
 * G233 PWM controller
*/
#include "qemu/osdep.h"
#include "hw/core/qdev.h"
#include "qemu/log.h"
#include "qemu/timer.h"
#include "hw/timer/g233_pwm.h"
#include "hw/core/irq.h"

static void g233_pwm_update_irq(G233PWMState *pwm)
{
    uint32_t pending = pwm->REG_PWM_GLB & 0x000000F0;
    qemu_set_irq(pwm->irq, pending != 0);     /* 根据DONE产生中断 */
}

static void doPWMTimerCallback(void *opaque)
{
    PWMChannel *chn = (PWMChannel *)opaque;
    uint64_t now = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);
    /* 根据设置去置位定时中断 */
    chn->cnt = 0;
    chn->parent->REG_PWM_GLB |= 1 << (chn->chn_id + 4);      /* 置位中断位 */

    g233_pwm_update_irq(chn->parent);
    timer_mod(&chn->timer, now + chn->period * PWM_TICK_NS);
}

/* 在外部发起读CNT寄存器请求时，执行这个函数 */
static uint32_t doPWMReadCNTCallback(void *opaque, uint32_t chn_id)
{
    G233PWMState *pwm = G233_PWM(opaque);
    if (pwm->chns[chn_id].period == 0 || !(pwm->chns[chn_id].ctrl & 0x1)) return 0;
    int64_t now_ns = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);
    int64_t elapsed = now_ns - pwm->chns[chn_id].start_ns;  
    if (!(pwm->chns[chn_id].ctrl & 0x1)) {
        return pwm->chns[chn_id].cnt;
    }
    return (uint32_t)((elapsed / PWM_TICK_NS) % pwm->chns[chn_id].period);
}

/*
寄存器映射表
    Offset	                寄存器	     访问	        复位值	           描述
0x10 + N*0x10 + 0x00	PWM_CHn_CTRL	R/W	        0x0000_0000	    通道控制寄存器
0x10 + N*0x10 + 0x04	PWM_CHn_PERIOD	R/W	        0x0000_0000	    周期值寄存器
0x10 + N*0x10 + 0x08	PWM_CHn_DUTY	R/W	        0x0000_0000	    占空比值寄存器
0x10 + N*0x10 + 0x0C	PWM_CHn_CNT	    R	        0x0000_0000	    当前计数值（只读）
*/
static uint64_t g233_pwm_read(void *opaque, hwaddr addr, unsigned int size)
{
    G233PWMState *pwm = G233_PWM(opaque);
    uint64_t value = 0;
    uint32_t cnt = 0;
    uint32_t chn_id = CHN_ID(addr);
    if (chn_id >= PWM_CHN_COUNT && addr != PWM_GLB)
    {
        qemu_log_mask(LOG_GUEST_ERROR, "%s: bad read offset 0x%" HWADDR_PRIx "\n",
                      __func__, addr);
        return 0;
    }
    // uint32_t idx = (addr >> 2) % 4;
    switch (addr)
    {
    /* DONE位会产生中断 */
    case PWM_GLB:
        value = pwm->REG_PWM_GLB;
        break;
    case PWM_CH_CTRL(0):
    case PWM_CH_CTRL(1):
    case PWM_CH_CTRL(2):
    case PWM_CH_CTRL(3):
        value = pwm->chns[chn_id].ctrl;
        break;
    case PWM_CH_PERIOD(0):
    case PWM_CH_PERIOD(1):
    case PWM_CH_PERIOD(2):
    case PWM_CH_PERIOD(3):
        value = pwm->chns[chn_id].period;
        break;
    case PWM_CH_DUTY(0):
    case PWM_CH_DUTY(1):
    case PWM_CH_DUTY(2):
    case PWM_CH_DUTY(3):
        value = pwm->chns[chn_id].duty;
        break;
    case PWM_CH_CNT(0):
    case PWM_CH_CNT(1):
    case PWM_CH_CNT(2):
    case PWM_CH_CNT(3):
        cnt = doPWMReadCNTCallback(opaque, chn_id);    /* 只在外界发起访问时，假装已经计数了，实际上我们是软件计算出来的 */
        pwm->chns[chn_id].cnt = cnt;   /* 设置回去 */
        value = cnt;
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR, "%s: bad read offset 0x%" HWADDR_PRIx "\n",
                      __func__, addr);
    }
    return value;
}

static void g233_pwm_write(void *opaque, hwaddr addr, uint64_t val64, unsigned int size)
{
    G233PWMState *pwm = G233_PWM(opaque);
    uint32_t chn_id = CHN_ID(addr);
    uint32_t prev;
    if (chn_id >= PWM_CHN_COUNT && addr != PWM_GLB)
    {
        qemu_log_mask(LOG_GUEST_ERROR, "%s: write: addr=0x%" HWADDR_PRIx
                  " val=0x%016" PRIx64 "\n",
                  __func__, addr, val64);
        return;
    }
    switch (addr)
    {
    /* DONE位会产生中断 */
    case PWM_GLB:
        pwm->REG_PWM_GLB &= ~((uint32_t)val64 & 0xf0);
        g233_pwm_update_irq(pwm);
        return;

    case PWM_CH_CTRL(0):
    case PWM_CH_CTRL(1):
    case PWM_CH_CTRL(2):
    case PWM_CH_CTRL(3):
        /* 低三位有效，其余位恒为0 */
        prev = pwm->chns[chn_id].ctrl;
        if (!(prev & 0x1) && (val64 & 0x1)){  /* 0->1 */
            pwm->REG_PWM_GLB |= 1 << chn_id;
            uint64_t now = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);
            pwm->chns[chn_id].start_ns = now;
            /* 启动一下用于定时中断的定时器 */
            timer_mod_ns(&pwm->chns[chn_id].timer, now + pwm->chns[chn_id].period * PWM_TICK_NS);
        } else if ((prev & 0x1) && !(val64 & 0x1)) {   /* 1->0 */
            pwm->REG_PWM_GLB &= ~(1 << chn_id);
            /* 清除strat_ns */
            pwm->chns[chn_id].start_ns = 0;
            pwm->chns[chn_id].cnt = 0;
            /* 清除DONE */
            pwm->REG_PWM_GLB &= ~(uint32_t)(1 << (chn_id + 4));
            g233_pwm_update_irq(pwm);
        } else {
            /* 1->1  0->0 */
            /* NOTHING TO BE DONE */
        }
        pwm->chns[chn_id].ctrl = ((uint32_t)val64) & 0x7;
        return;

    case PWM_CH_PERIOD(0):
    case PWM_CH_PERIOD(1):
    case PWM_CH_PERIOD(2):
    case PWM_CH_PERIOD(3):
        /* DUTY <= PERIOD */
        if (val64 < pwm->chns[chn_id].duty)
        {
            qemu_log_mask(LOG_GUEST_ERROR, "%s: write: addr=0x%" HWADDR_PRIx
                  " val=0x%016" PRIx64 "PERIOD should be greater than DUTY\n",
                  __func__, addr, val64);
            return;
        }
        pwm->chns[chn_id].period = (uint32_t)val64; 
        if (!(pwm->chns[chn_id].ctrl & 0x1)) return;
        /* 要同步设置定时器 */
        pwm->chns[chn_id].cnt = 0;
        pwm->REG_PWM_GLB &= ~(uint32_t)(1 << (chn_id + 4));    /* 清除中断 */
        g233_pwm_update_irq(pwm);
        uint64_t new_now = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);
        pwm->chns[chn_id].start_ns = new_now;
        /* 启动一下用于定时中断的定时器 */ 
        timer_mod_ns(&pwm->chns[chn_id].timer, new_now + pwm->chns[chn_id].period * PWM_TICK_NS);
        return;

    case PWM_CH_DUTY(0):
    case PWM_CH_DUTY(1):
    case PWM_CH_DUTY(2):
    case PWM_CH_DUTY(3):
        /* DUTY <= PERIOD */
        if (val64 > pwm->chns[chn_id].period)
        {
            qemu_log_mask(LOG_GUEST_ERROR, "%s: write: addr=0x%" HWADDR_PRIx
                  " val=0x%016" PRIx64 "PERIOD should be greater than DUTY\n",
                  __func__, addr, val64);
            return;
        }
        pwm->chns[chn_id].duty = (uint32_t)val64;
        return;
    default:
        break;
    }
    qemu_log_mask(LOG_GUEST_ERROR, "%s: write: addr=0x%" HWADDR_PRIx
                  " val=0x%016" PRIx64 "\n",
                  __func__, addr, val64);
}

static const MemoryRegionOps g233_pwm_ops = {
    .read = g233_pwm_read,
    .write = g233_pwm_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
    /* 4 bytes aligned */
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4
    }
};

/* 下面这些都是为了让qemu可以发现这个设备 */
static void g233_pwm_init(Object *obj)
{
    G233PWMState *pwm = G233_PWM(obj);
    /* 初始化MEIO,接入总线 */
    memory_region_init_io(&pwm->mmio, obj, &g233_pwm_ops, pwm, TYPE_G233_PWM, 0x1000);
    sysbus_init_mmio(SYS_BUS_DEVICE(obj), &pwm->mmio);
    sysbus_init_irq(SYS_BUS_DEVICE(obj), &pwm->irq);
    /* 初始化定时器并注册回调，在回调中设置计数值 */
    for (int i = 0; i < PWM_CHN_COUNT; i++)
    {
        pwm->chns[i].parent = pwm;
        pwm->chns[i].chn_id = i;
        timer_init_ns(&pwm->chns[i].timer, QEMU_CLOCK_VIRTUAL, doPWMTimerCallback, &pwm->chns[i]);
    }
}

static const TypeInfo g233_pwm_info = {
    .name = TYPE_G233_PWM,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(G233PWMState),
    .instance_init = g233_pwm_init,
};

static void g233_pwm_register_types(void)
{
    type_register_static(&g233_pwm_info);
}

type_init(g233_pwm_register_types)