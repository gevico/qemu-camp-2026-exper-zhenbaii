#include "qemu/osdep.h"
#include "hw/core/irq.h"
#include "hw/core/sysbus.h"
#include "hw/ssi/g233_spi.h"
#include "hw/ssi/ssi.h"
#include "qemu/log.h"
#include "qemu/module.h"
#include "qapi/error.h"

static void g233_spi_update_irq(G233SPIState *spi)
{
    uint32_t cr1 = spi->regs[G233_SPI_CR1 >> 2];
    uint32_t sr  = spi->regs[G233_SPI_SR >> 2];

    bool rxne    = (sr & G233_SPI_SR_RXNE)    && (cr1 & G233_SPI_CR1_RXNEIE);
    bool txe     = (sr & G233_SPI_SR_TXE)     && (cr1 & G233_SPI_CR1_TXEIE);
    bool overrun = (sr & G233_SPI_SR_OVERRUN) && (cr1 & G233_SPI_CR1_ERRIE);

    qemu_set_irq(spi->irq, (rxne || txe || overrun) ? 1 : 0);
}

static void g233_spi_update_cs(G233SPIState *spi)
{
    uint32_t cs_sel = (spi->regs[G233_SPI_CR2 >> 2] >> G233_SPI_CR2_CS_SHIFT)
                      & G233_SPI_CR2_CS_MASK;
    int i;

    for (i = 0; i < G233_SPI_NUM_CS; i++) {
        qemu_set_irq(spi->cs_lines[i], (i == cs_sel) ? 0 : 1);
    }
}

static void g233_spi_reset(DeviceState *dev)
{
    G233SPIState *spi = G233_SPI(dev);
    int i;

    memset(spi->regs, 0, sizeof(spi->regs));
    spi->regs[G233_SPI_SR >> 2] = G233_SPI_SR_TXE;

    for (i = 0; i < G233_SPI_NUM_CS; i++) {
        qemu_set_irq(spi->cs_lines[i], 1);
    }
}

static uint64_t g233_spi_read(void *opaque, hwaddr addr, unsigned int size)
{
    G233SPIState *spi = G233_SPI(opaque);

    switch (addr) {
    case G233_SPI_CR1:
    case G233_SPI_CR2:
        return spi->regs[addr >> 2];

    case G233_SPI_SR:
        return spi->regs[G233_SPI_SR >> 2];

    case G233_SPI_DR:
    {
        uint32_t val = spi->regs[G233_SPI_DR >> 2] & G233_SPI_DR_DATA;

        spi->regs[G233_SPI_SR >> 2] &= ~G233_SPI_SR_RXNE;
        g233_spi_update_irq(spi);

        return val;
    }

    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: bad read at offset 0x%" HWADDR_PRIx "\n",
                      __func__, addr);
        return 0;
    }
}

static void g233_spi_do_transfer(G233SPIState *spi, uint8_t tx)
{
    uint32_t sr = spi->regs[G233_SPI_SR >> 2];

    if (sr & G233_SPI_SR_RXNE) {
        spi->regs[G233_SPI_SR >> 2] |= G233_SPI_SR_OVERRUN;
    }

    spi->regs[G233_SPI_DR >> 2] = ssi_transfer(spi->spi, tx);
    spi->regs[G233_SPI_SR >> 2] |= G233_SPI_SR_RXNE;

    g233_spi_update_irq(spi);
}

static void g233_spi_write(void *opaque, hwaddr addr,
                           uint64_t val64, unsigned int size)
{
    G233SPIState *spi = G233_SPI(opaque);
    uint32_t value = (uint32_t)val64;

    switch (addr) {
    case G233_SPI_CR1:
        spi->regs[G233_SPI_CR1 >> 2] = value;
        g233_spi_update_irq(spi);
        break;

    case G233_SPI_CR2:
        spi->regs[G233_SPI_CR2 >> 2] = value;
        g233_spi_update_cs(spi);
        break;

    case G233_SPI_SR:
        if (value & G233_SPI_SR_OVERRUN) {
            spi->regs[G233_SPI_SR >> 2] &= ~G233_SPI_SR_OVERRUN;
            g233_spi_update_irq(spi);
        }
        break;

    case G233_SPI_DR:
        if (spi->regs[G233_SPI_CR1 >> 2] & G233_SPI_CR1_SPE) {
            g233_spi_do_transfer(spi, value & G233_SPI_DR_DATA);
        }
        break;

    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: bad write at offset 0x%" HWADDR_PRIx
                      " val=0x%" PRIx32 "\n",
                      __func__, addr, value);
        break;
    }
}

static const MemoryRegionOps g233_spi_ops = {
    .read = g233_spi_read,
    .write = g233_spi_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4
    }
};

static void g233_spi_realize(DeviceState *dev, Error **errp)
{
    G233SPIState *spi = G233_SPI(dev);
    SysBusDevice *sbd = SYS_BUS_DEVICE(dev);
    int i;

    memory_region_init_io(&spi->mmio, OBJECT(spi), &g233_spi_ops, spi,
                          TYPE_G233_SPI, 0x100);
    sysbus_init_mmio(sbd, &spi->mmio);

    sysbus_init_irq(sbd, &spi->irq);

    spi->spi = ssi_create_bus(dev, "spi");

    for (i = 0; i < G233_SPI_NUM_CS; i++) {
        sysbus_init_irq(sbd, &spi->cs_lines[i]);
    }
}

static void g233_spi_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    device_class_set_legacy_reset(dc, g233_spi_reset);
    dc->realize = g233_spi_realize;
}

static const TypeInfo g233_spi_info = {
    .name          = TYPE_G233_SPI,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(G233SPIState),
    .class_init    = g233_spi_class_init,
};

static void g233_spi_register_types(void)
{
    type_register_static(&g233_spi_info);
}

type_init(g233_spi_register_types)
