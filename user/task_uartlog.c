/**
 * @file    task_uartlog.c
 * @brief   UART送信機能の負荷確認用ログタスクを提供する。
 *
 * @details
 * 2つのタスクが異なる周期でカウンタを更新し、ログ出力試験が有効な場合は
 * UART送信キューへカウンタ値を送る。試験出力はコンパイル時設定で有効化する。
 */

#include <trykernel.h>

#include "uart_tx.h"

#define ENABLE_UART_LOG_TEST 0 //!< UARTログ試験出力を有効にする場合は1。

/**
 * @brief A系ログタスクを実行する。
 * @param[in] stacd タスク起動コード（この関数では使用しない）。
 * @param[in] exinf タスク拡張情報（この関数では使用しない）。
 * @details カウンタを増加させながら1900 ms周期で動作する。
 *          ENABLE_UART_LOG_TESTが有効な場合はカウンタをUARTへ出力する。
 */
void task_uartlog_a(INT stacd, void *exinf)
{
    (void)stacd;
    (void)exinf;
    UW count = 0;
    while (TRUE) {
#if ENABLE_UART_LOG_TEST
        uart_tx_printf("LOG A count: %u\r\n", count);
#endif
        count++;
        tk_dly_tsk(1900);
    }
}

/**
 * @brief B系ログタスクを実行する。
 * @param[in] stacd タスク起動コード（この関数では使用しない）。
 * @param[in] exinf タスク拡張情報（この関数では使用しない）。
 * @details カウンタを増加させながら2300 ms周期で動作する。
 *          ENABLE_UART_LOG_TESTが有効な場合はカウンタをUARTへ出力する。
 */
void task_uartlog_b(INT stacd, void *exinf)
{
    (void)stacd;
    (void)exinf;
    UW count = 0;
    while (TRUE) {
#if ENABLE_UART_LOG_TEST
        uart_tx_printf("LOG B count: %u\r\n", count);
#endif
        count++;
         tk_dly_tsk(2300);
    }
}
