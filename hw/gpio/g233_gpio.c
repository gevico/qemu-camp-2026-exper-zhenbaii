/*
 * GPIO controller for G233
*/

#include "qemu/osdep.h"
#include "hw/gpio/g233_gpio.h"
#include "hw/core/sysbus.h"
#include "qapi/error.h"
#include "qemu/log.h"
#include "qemu/module.h"

/*
    寄存器映射：
    Offset	寄存器	        访问	    复位值	    描述
    0x00	GPIO_DIR	    R/W	    0x0000_0000	方向寄存器
    0x04	GPIO_OUT	    R/W	    0x0000_0000	输出数据寄存器
    0x08	GPIO_IN	        R	    0x0000_0000	输入数据寄存器
    0x0C	GPIO_IE	        R/W	    0x0000_0000	中断使能寄存器
    0x10	GPIO_IS	        R/W	    0x0000_0000	中断状态寄存器
    0x14	GPIO_TRIG	    R/W	    0x0000_0000	中断触发类型寄存器
    0x18	GPIO_POL	    R/W	    0x0000_0000	中断极性寄存器
*/
/* 寄存器位宽都是4个字节，因此一般不会用到size，而是通过.valid来限制 */
static uint64_t g233_gpio_read(void *opaque, hwaddr addr, unsigned int size)
{
    uint32_t value = 0;    /* register */
    G233GPIOState *g = G233_GPIO(opaque);
    switch (addr)
    {
    case G233_GPIO_DIR:
    case G233_GPIO_OUT:
    case G233_GPIO_IN:
    case G233_GPIO_IE:
    case G233_GPIO_IS:
    case G233_GPIO_TRIG:
    case G233_GPIO_POL:
        value = g->regs[addr >> 2];
        break;
    /* 非法地址 */
    default:
        qemu_log_mask(LOG_GUEST_ERROR, "%s: bad read offset 0x%" HWADDR_PRIx "\n",
              __func__, addr);
    }
    return value;
}

/* 写的时候就需要发生相应的动作 */
/* 引脚为输入模式时，其电平值由外部决定，输出模式时，则由DIR和OUT寄存器来决定 */
static void g233_gpio_write(void *opaque, hwaddr addr, uint64_t val64, unsigned int size)
{
    G233GPIOState *g = G233_GPIO(opaque);
    switch (addr)
    {
    case G233_GPIO_DIR:
        g->regs[addr >> 2] = (uint32_t)val64;
        /* OUT寄存了用户期望的输出值，因此这里需要更新IN寄存器 */
        g->regs[G233_GPIO_IN >> 2] = (uint32_t)val64 & (g->regs[G233_GPIO_OUT >> 2]);
        return;
    /* GPIO_OUT不仅反映当前各个引脚的输出情况，同时可以和DIR配合，来控制引脚的输出 */
    case G233_GPIO_OUT:
        g->regs[addr >> 2] = (uint32_t)val64;
        g->regs[G233_GPIO_IN >> 2] = (uint32_t)val64 & g->regs[G233_GPIO_DIR >> 2]; /* 仅当DIR为输出时，才有效 */
        return;
    case G233_GPIO_IE:
        g->regs[addr >> 2] = (uint32_t)val64;
        /* TODO: 改变中断状态对应的具体操作 */
        return;
    case G233_GPIO_IS:
        g->regs[addr >> 2] = (uint32_t)val64;
        /* TODO: 改变中断状态对应的具体操作 */
        return;
    case G233_GPIO_TRIG:
        g->regs[addr >> 2] = (uint32_t)val64;
        /* TODO: 改变中断触发方式对应的具体操作 */
        return;
    case G233_GPIO_POL:
        g->regs[addr >> 2] = (uint32_t)val64;
        /* TODO: 改变中断极性对应的具体操作 */
        return;
    default:
        break;
    }
    qemu_log_mask(LOG_GUEST_ERROR, "%s: write: addr=0x%" HWADDR_PRIx
              " val=0x%016" PRIx64 "\n",
              __func__, addr, val64);
}

static const MemoryRegionOps g233_gpio_ops = {
    .read = g233_gpio_read,
    .write = g233_gpio_write,
    .endianness = DEVICE_NATIVE_ENDIAN,   /* 大小端 */
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4
    }
};

static void g233_gpio_init(Object *obj)
{
    G233GPIOState *g = G233_GPIO(obj);
    memory_region_init_io(&g->mmio, obj, &g233_gpio_ops, g, TYPE_G233_GPIO, 0x100);
    sysbus_init_mmio(SYS_BUS_DEVICE(obj), &g->mmio);
}

static const TypeInfo g233_gpio_info = {
    .name = TYPE_G233_GPIO,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(G233GPIOState),
    .instance_init = g233_gpio_init,
};

static void g233_gpio_register_types(void)
{
    type_register_static(&g233_gpio_info);
}

type_init(g233_gpio_register_types)


