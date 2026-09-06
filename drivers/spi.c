/*
 * SPI0 polling driver for TryKernel / RP2040
 *
 * SPI0 Mode 0, 8 bit, approximately 1 MHz:
 *   SCK  = GPIO18
 *   MOSI = GPIO19
 *   MISO = GPIO20
 */
#include <trykernel.h>
#include "gpio.h"
#include "spi.h"

#define SPI0_TIMEOUT_LOOP  100000U
#define SPI0_CPSDVSR       2U
#define SPI0_SCR           62U

void spi0_init(void)
{
    UW reset_mask = RESETS_RESET_SPI0;

    out_w(RESETS_RESET, in_w(RESETS_RESET) | reset_mask);
    out_w(RESETS_RESET, in_w(RESETS_RESET) & ~reset_mask);
    while((in_w(RESETS_RESET_DONE) & reset_mask) == 0U);

    gpio_init_spi(SPI0_SCK_PIN);
    gpio_init_spi(SPI0_MOSI_PIN);
    gpio_init_spi(SPI0_MISO_PIN);

    /* 設定中はSPI0を停止する */
    out_w(SPI0_BASE + SPIx_CR1, 0U);

    /* 8ビット、Motorola SPI Mode 0、約1MHz */
    out_w(
        SPI0_BASE + SPIx_CR0,
        SPI_CR0_DSS_8BIT | SPI_CR0_SCR(SPI0_SCR)
    );
    out_w(SPI0_BASE + SPIx_CPSR, SPI0_CPSDVSR);

    /* ポーリング方式のため割り込みは使用しない */
    out_w(SPI0_BASE + SPIx_IMSC, 0U);
    out_w(SPI0_BASE + SPIx_ICR, 0x03U);
    /* SPI0を有効にする */
    out_w(SPI0_BASE + SPIx_CR1, SPI_CR1_SSE);
}
/* 1バイト送信、1バイト受信 */
BOOL spi0_transfer(UB tx_data, UB *rx_data)
{
    UW count;

    if(rx_data == NULL) return FALSE;

    for(count = 0U; count < SPI0_TIMEOUT_LOOP; count++){
        /* 送信可能か確認 */
        if((in_w(SPI0_BASE + SPIx_SR) & SPI_SR_TNF) != 0U){
            break;
        }
    }
    if(count >= SPI0_TIMEOUT_LOOP) return FALSE;

    out_w(SPI0_BASE + SPIx_DR, tx_data);

    for(count = 0U; count < SPI0_TIMEOUT_LOOP; count++){
        /* 受信可能か確認 */
        if((in_w(SPI0_BASE + SPIx_SR) & SPI_SR_RNE) != 0U){
            *rx_data = (UB)in_w(SPI0_BASE + SPIx_DR);
            return TRUE;
        }
    }

    return FALSE;
}

/* 送受信が完了し、SPI0がアイドル状態になるまで待つ */
BOOL spi0_wait_idle(void)
{
    UW count;

    for(count = 0U; count < SPI0_TIMEOUT_LOOP; count++){
        if((in_w(SPI0_BASE + SPIx_SR) & SPI_SR_BSY) == 0U){
            return TRUE;
        }
    }

    return FALSE;
}
