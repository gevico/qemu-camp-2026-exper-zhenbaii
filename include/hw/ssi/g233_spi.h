#ifndef HW_G233_SPI_H
#define HW_G233_SPI_H

#include "hw/core/sysbus.h"
#include "hw/ssi/ssi.h"
#include "qemu/timer.h"
#include "qom/object.h"

#define TYPE_G233_SPI "riscv.g233.spi"

/*
Offset	寄存器	     访问	    复位值	                描述
0x00	SPI_CR1	    R/W	    0x0000_0000	    控制寄存器 1（使能/主从/中断使能）
0x04	SPI_CR2	    R/W	    0x0000_0000	    控制寄存器 2（片选选择）
0x08	SPI_SR	    R/W	    0x0000_0002	            状态寄存器
0x0C	SPI_DR	    R/W	    0x0000_0000	            数据寄存器
*/
#define G233_SPI_CR1 0x0000
#define G233_SPI_CR1_SPE (1U << 0)
#define G233_SPI_CR1_MSTR (1U << 2)
#define G233_SPI_CR1_ERRIE (1U << 5)
#define G233_SPI_CR1_RXNEIE (1U << 6)
#define G233_SPI_CR1_TXEIE (1U << 7)

#define G233_SPI_CR2 0x0004
#define G233_SPI_CR2_CS_MASK 0x3
#define G233_SPI_CR2_CS_SHIFT 0

#define G233_SPI_SR  0x0008
#define G233_SPI_SR_RXNE (1U << 0)
#define G233_SPI_SR_TXE (1U << 1)
#define G233_SPI_SR_OVERRUN (1U << 4)

#define G233_SPI_DR  0x000c
#define G233_SPI_DR_DATA 0x000000ff
#define G233_SPI_NREGS 4

typedef struct G233SPIState G233SPIState;
DECLARE_INSTANCE_CHECKER(G233SPIState, G233_SPI, TYPE_G233_SPI);

#define G233_SPI_NUM_CS 4

struct G233SPIState {
    SysBusDevice parent;
    MemoryRegion mmio;
    uint32_t regs[G233_SPI_NREGS];

    qemu_irq irq;
    SSIBus *spi;
    qemu_irq cs_lines[G233_SPI_NUM_CS];
};

#endif