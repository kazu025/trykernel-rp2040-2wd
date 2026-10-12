#ifndef UART_H
#define UART_H
/*
 * Uart driver for Try Kernel / RP2040 UART0
 * This driver provides polling-based transmit and interrupt-driven receive.
 * Low-level register access is kept inside this driver.
 */
#include <trykernel.h>
void uart_init(void);           // UART0を初期化する
void uart_rx_irq_enable(void);  // UART0受信割り込み有効化する
void uart0_irq_handler(void);   // UART0受信ハンドラ
BOOL uart_can_send(void);       // UART送信FIFOに空きがあるかを確認する
BOOL uart_can_recv(void);       // UART受信FIFOにデータがあるか確認する
void uart_putc(UB c);           // １バイト送信
UINT uart_puts(const char *s);  // 文字列送信
BOOL uart_getc_nonblock(UB *c); // １バイトノンブロッキング送信
UB uart_getc(void);             // UARTから１バイト受信
int uart_rx_getc(UB *data);     // リングバッファから１文字取り出す
void uart_rxbuf_init(void);         // 受信バッファの初期化
UW uart_rx_overflow_count(void);    // 受信バッファのオーバーフロー回数を返す
UW uart_rx_hw_overrun_count(void);  // オーバーランカウントを返す
UW uart_rx_irq_count_get(void);      // 受信割り込み回数を返す
UW uart_rt_irq_count_get(void);      // 受信タイムアウト割り込み回数を返す

typedef void (*UART_RX_NOTIFY_FUNC)(void);
/*
 * 受信データ格納時に割り込みコンテキストから呼び出す
 * 通知関数を登録する
 */
void uart_rx_set_notify(UART_RX_NOTIFY_FUNC notify);
#endif /* UART_H */
