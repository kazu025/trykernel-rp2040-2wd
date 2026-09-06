#ifndef RP2040_SPI_H
#define RP2040_SPI_H

#include <trykernel.h>

#define SPI0_SCK_PIN   18U
#define SPI0_MOSI_PIN  19U
#define SPI0_MISO_PIN  20U

void spi0_init(void);
BOOL spi0_transfer(UB tx_data, UB *rx_data);
BOOL spi0_wait_idle(void);

#endif /* RP2040_SPI_H */
