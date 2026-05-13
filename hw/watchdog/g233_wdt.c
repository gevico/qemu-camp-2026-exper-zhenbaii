#include "qemu/osdep.h"
#include "hw/watchdog/g233_wdt.h"
#include "hw/core/irq.h"
#include "qemu/log.h"

static void g233_wdt_update_irq(G233WDTState *wdt)
{
    uint32_t pending = (wdt->regs[G233_WDT_SR >> 2] & G233_WDT_SR_TIMEOUT)
                     && (wdt->regs[G233_WDT_CTRL >> 2] & G233_WDT_CTRL_INTEN);
    qemu_set_irq(wdt->irq, pending != 0);
}

static void doWDTimerCallback(void *opaque)
{
    G233WDTState *wdt = G233_WDT(opaque);
    wdt->regs[G233_WDT_SR >> 2] |= G233_WDT_SR_TIMEOUT;
    /* 不需要重新装计数器了，直接通知plic */
    if (wdt->regs[G233_WDT_CTRL >> 2] & G233_WDT_CTRL_INTEN) {
        g233_wdt_update_irq(wdt);
    } 
    if (wdt->regs[G233_WDT_CTRL >> 2] & G233_WDT_CTRL_RSTEN) {
        /* TODO */
    }
}

static uint32_t doWDTReadCNT(G233WDTState *wdt)
{
    /* 返回锁存值 */
    if (!(wdt->regs[G233_WDT_CTRL >> 2] & G233_WDT_CTRL_EN)) {
        return wdt->regs[G233_WDT_VAL >> 2];
    }
    uint64_t now = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);
    uint64_t elapsed = now - wdt->start_ns;   /* start_ns需要在每次喂狗的时候更新 */
    uint64_t elapsed_ticks = elapsed / WDT_TICK_NS;

    if (elapsed_ticks >= (uint64_t)wdt->regs[G233_WDT_LOAD >> 2]) {
        return 0; 
    }
    // 5. 只有在还没减到 0 的情况下，才返回剩余的真实计数值
    return (uint32_t)(wdt->regs[G233_WDT_LOAD >> 2] - elapsed_ticks); 
}

static void doWriteWDTKey(G233WDTState *wdt, uint32_t key)
{
    if (key == G233_WDT_KEY_FEED) {
        uint64_t now = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);
        wdt->start_ns = now;
        wdt->regs[G233_WDT_VAL >> 2] = wdt->regs[G233_WDT_LOAD >> 2];

        wdt->regs[G233_WDT_SR >> 2] &= ~G233_WDT_SR_TIMEOUT;
        g233_wdt_update_irq(wdt);
        
        if (wdt->regs[G233_WDT_CTRL >> 2] & G233_WDT_CTRL_EN) {
            /* 使能状态下喂狗，需要重置定时器超时时间 */
            timer_mod(&wdt->timer, now + WDT_TICK_NS * wdt->regs[G233_WDT_VAL >> 2]);
        }
    } else if (key == G233_WDT_KEY_LOCK) {
        wdt->regs[G233_WDT_CTRL >> 2] |= G233_WDT_CTRL_LOCK;
    } else {
        qemu_log_mask(LOG_GUEST_ERROR, "%s: unsupported key 0x%" PRIx32  "\n",
                     __func__, key);
    }
}

static void doWriteWDTCtrlReg(G233WDTState *wdt, uint32_t value)
{
    uint32_t prev = wdt->regs[G233_WDT_CTRL >> 2];
    uint32_t changed = prev ^ value;
    /* 设备的使能与否不应该作为其他位是否可以成功写入的条件，因为现实中通常就是先配置设备再使能 */
    if (changed & G233_WDT_CTRL_EN) {
        /* 0->1 */
        if (value & G233_WDT_CTRL_EN) {
            uint32_t spent_ticks = wdt->regs[G233_WDT_LOAD >> 2] - wdt->regs[G233_WDT_VAL >> 2];
            uint64_t now = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);
            wdt->start_ns = now - spent_ticks * WDT_TICK_NS;
            timer_mod(&wdt->timer, now + WDT_TICK_NS * wdt->regs[G233_WDT_VAL >> 2]);
        }
        /* 1->0 */
        else {
            wdt->regs[G233_WDT_VAL >> 2] = doWDTReadCNT(wdt);   /* 锁存 */
            timer_del(&wdt->timer);
        }
    }
    if (changed & G233_WDT_CTRL_INTEN) {
        /* 0->1 */
        if (value & G233_WDT_CTRL_INTEN) {
        }
        /* 1->0 */
        else {
        }
    }
    if (changed & G233_WDT_CTRL_RSTEN) {
        /* 0->1 */
        if (value & G233_WDT_CTRL_RSTEN) {
        }
        /* 1->0 */
        else {
        }
    }
}

static void g233_wdt_reset(DeviceState *dev)
{
    G233WDTState *wdt = G233_WDT(dev);
    wdt->regs[G233_WDT_CTRL >> 2] = 0;
    wdt->regs[G233_WDT_LOAD >> 2] = 0xffff;
    wdt->regs[G233_WDT_VAL >> 2] = 0xffff;
    wdt->regs[G233_WDT_SR >> 2] = 0;
}

/*
Offset	寄存器	    访问	    复位值	        描述
0x00	WDT_CTRL	R/W	    0x0000_0000	    控制寄存器
0x04	WDT_LOAD	R/W	    0x0000_FFFF	    装载值寄存器
0x08	WDT_VAL	    R	    0x0000_FFFF	    当前计数值（只读）
0x0C	WDT_SR	    R/W	    0x0000_0000	    状态寄存器
0x10	WDT_KEY	    W	        —	        密钥寄存器（只写）
*/
static uint64_t g233_wdt_read(void *opaque, hwaddr addr, unsigned int size)
{
    G233WDTState *wdt = G233_WDT(opaque);
    uint64_t value = 0;
    switch (addr)
    {
    case G233_WDT_CTRL:
        value = wdt->regs[addr >> 2];
        break;
    case G233_WDT_LOAD:
        value = wdt->regs[addr >> 2];
        break;
    case G233_WDT_VAL:
        value = doWDTReadCNT(wdt);
        wdt->regs[addr >> 2] = value;
        break;
    case G233_WDT_SR:
        value = wdt->regs[addr >> 2];
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR, "%s: bad read offset 0x%" HWADDR_PRIx "\n",
                  __func__, addr);
        break;
    }
    return value;
}

static void g233_wdt_write(void *opaque, hwaddr addr, uint64_t val64, unsigned int size)
{
    G233WDTState *wdt = G233_WDT(opaque);
    switch (addr)
    {
    case G233_WDT_CTRL:
        if (wdt->regs[G233_WDT_CTRL >> 2] & G233_WDT_CTRL_LOCK) {
            qemu_log_mask(LOG_GUEST_ERROR, "%s: lock bit set, cannot write\n",
                         __func__);
            break;
        }
        doWriteWDTCtrlReg(wdt, val64);
        wdt->regs[addr >> 2] = (uint32_t)val64 & WDT_CTRL_VALID_MASK;
        break;
    case G233_WDT_LOAD:
        if (!(wdt->regs[G233_WDT_CTRL >> 2] & G233_WDT_CTRL_EN))
            wdt->regs[G233_WDT_VAL >> 2] = (uint32_t)val64;     /* 如果在关闭使能的情况下写，就重置CNT */
        wdt->regs[G233_WDT_LOAD >> 2] = (uint32_t)val64;
        break;
    case G233_WDT_SR:
        wdt->regs[addr >> 2] &= ~((uint32_t)val64 & G233_WDT_SR_VALID_MASK);
        g233_wdt_update_irq(wdt);
        break;
    case G233_WDT_KEY:
        doWriteWDTKey(wdt, (uint32_t)val64);
        /* 这种只写寄存器实际上不存在物理实体，只是总线发现写入的值正确后去匹配一些动作，因此不用维护这个寄存器 */
        // wdt->regs[addr >> 2] = doWriteWDTKey(wdt, (uint32_t)val64) ? (uint32_t)val64 : 0;
        break;
    }
}

static const MemoryRegionOps g233_wdt_ops = {
    .read = g233_wdt_read,
    .write = g233_wdt_write,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4
    },
    .endianness = DEVICE_NATIVE_ENDIAN
};

static void g233_wdt_init(Object *obj)
{
    G233WDTState *wdt = G233_WDT(obj);
    memory_region_init_io(&wdt->mmio, obj, &g233_wdt_ops, wdt, TYPE_G233_WDT, 0x1000);
    sysbus_init_mmio(SYS_BUS_DEVICE(obj), &wdt->mmio);
    sysbus_init_irq(SYS_BUS_DEVICE(obj), &wdt->irq);
    timer_init_ns(&wdt->timer, QEMU_CLOCK_VIRTUAL, doWDTimerCallback, wdt);
}

static void g233_wdt_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    device_class_set_legacy_reset(dc, g233_wdt_reset);
}

static const TypeInfo g233_wdt_info = {
    .name          = TYPE_G233_WDT,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(G233WDTState),
    .instance_init = g233_wdt_init,
    .class_init    = g233_wdt_class_init,
};

static void g233_wdt_register_types(void)
{
    type_register_static(&g233_wdt_info);
}

type_init(g233_wdt_register_types)