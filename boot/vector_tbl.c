/**
 * @file    vector_tbl.c
 * @brief   Cortex-M例外およびRP2040割り込みのベクターテーブルを定義する。
 *
 * @details ベクターテーブルを.vectorセクションへ配置し、リセット、PendSV、SysTick、
 *          GPIO、UART0の各ハンドラを登録する。未使用の例外・割り込みは
 *          Default_Handlerへ割り当てる。
 */

#include <typedef.h>
#include <sysdef.h>
#include <syslib.h>
#include <knldef.h>

extern void dispatch_entry(void);       /* ディスパッチャ */
extern void uart0_irq_handler(void);    /* UART0受信割り込み */
extern void io_irq_bank0_handler(void); /* GPIO割り込み */
/**
 * @brief 未使用の例外・割り込みで実行を停止する。
 * @details 予期しない例外または未割り当て割り込みが発生した場合、
 *          無限ループへ入り以降の処理を行わない。
 */
void Default_Handler(void)
{
    while(1);
}

/**
 * @brief Cortex-M例外とRP2040割り込みのハンドラーアドレスを保持するベクターテーブル。
 * @details リンカスクリプトにより.vectorセクションへ配置され、リセット直後に
 *          Cortex-Mから参照される。先頭要素には初期スタックポインターを格納する。
 */
void (* const vector_tbl[])() __attribute__((section(".vector"))) = {
    (void(*)()) (INITIAL_SP),   // 0: Top of Stack
    Reset_Handler,              // 1: Reset
    Default_Handler,            // 2: NMI
    Default_Handler,            // 3: Hard Fault
    0,                          // 4: 未使用
    0,                          // 5: 未使用
    0,                          // 6: 未使用
    0,                          // 7: 未使用
    0,                          // 8: 未使用
    0,                          // 9: 未使用
    0,                          // 10: 未使用
    Default_Handler,            // 11: Svcall
    0,                          // 12: 未使用
    0,                          // 13: 未使用
    dispatch_entry,             // 14: Pend SV
    systimer_handler,           // 15: Systick
    Default_Handler,            // IRQ 0
    Default_Handler,            // IRQ 1
    Default_Handler,            // IRQ 2
    Default_Handler,            // IRQ 3
    Default_Handler,            // IRQ 4
    Default_Handler,            // IRQ 5
    Default_Handler,            // IRQ 6
    Default_Handler,            // IRQ 7
    Default_Handler,            // IRQ 8
    Default_Handler,            // IRQ 9
    Default_Handler,            // IRQ 10
    Default_Handler,            // IRQ 11
    Default_Handler,            // IRQ 12
    io_irq_bank0_handler,       // IRQ 13: IO_BANK0
    Default_Handler,            // IRQ 14
    Default_Handler,            // IRQ 15
    Default_Handler,            // IRQ 16
    Default_Handler,            // IRQ 17
    Default_Handler,            // IRQ 18
    Default_Handler,            // IRQ 19
    uart0_irq_handler,          // IRQ 20: UART0
    Default_Handler,            // IRQ 21
    Default_Handler,            // IRQ 22
    Default_Handler,            // IRQ 23
    Default_Handler,            // IRQ 24
    Default_Handler,            // IRQ 25
    Default_Handler,            // IRQ 26
    Default_Handler,            // IRQ 27
    Default_Handler,            // IRQ 28
    Default_Handler,            // IRQ 29
    Default_Handler,            // IRQ 31
};
