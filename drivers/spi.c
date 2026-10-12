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

/**
 * @brief SPI0コントローラを初期化する。
 *
 * @details
 * SPI0周辺回路をリセットしてリセット解除を確認した後、
 * SCK、MOSI、MISOに使用するGPIOをSPI機能へ設定する。
 *
 * SPI0は次の設定で初期化する。
 *
 * - 8ビットデータ
 * - Motorola SPI Mode 0
 * - SPIクロック約1MHz
 * - ポーリングによる通信
 * - SPI割込み無効
 *
 * 設定中はSPI0を停止し、クロックプリスケーラなどの設定完了後に
 * SPI0を有効化する。
 *
 * @note
 * SPI0のリセット完了を待つ処理にはタイムアウトがない。
 * また、チップセレクト（CS）の制御はこの関数では行わない。
 */
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
/**
 * @brief SPI0で1バイト送受信する。
 *
 * @details
 * SPI0の送信FIFOに空きができるまで待機し、tx_dataを送信する。
 * その後、受信FIFOにデータが到着するまで待機し、
 * 受信した1バイトをrx_dataへ格納する。
 *
 * SPIは全二重通信のため、1バイトを送信すると同時に
 * 1バイトを受信する。
 *
 * @param[in]  tx_data SPI0から送信する1バイトのデータ。
 * @param[out] rx_data SPI0から受信した1バイトを格納する領域。
 *
 * @retval TRUE 送信および受信に成功した。
 * @retval FALSE rx_dataがNULL、送信FIFOが満杯のままタイムアウト、
 *               または受信FIFOにデータが到着せずタイムアウトした。
 *
 * @note
 * 待ち時間はSPI0_TIMEOUT_LOOPとCPUの動作速度に依存する。
 * 受信のみを行う場合でも、SPIのクロックを発生させるために
 * ダミーデータを送信する必要がある。
 */
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
/**
 * @brief SPI0の送受信完了とアイドル状態を待つ。
 *
 * @details
 * SPI0のステータスレジスタをポーリングし、
 * SPI_SR_BSYビットがクリアされるまで待機する。
 *
 * SPI_SR_BSYがクリアされると、SPI0が送受信を完了して
 * アイドル状態になったと判断する。
 *
 * @retval TRUE  SPI0がアイドル状態になった。
 * @retval FALSE SPI0がビジー状態のままタイムアウトした。
 *
 * @note
 * 待ち時間はSPI0_TIMEOUT_LOOPとCPUの動作速度に依存する。
 * 最終送信データの完了を確認してからCSを解除する場合などに使用する。
 */
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
