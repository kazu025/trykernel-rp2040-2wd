#include <trykernel.h>
#include "uart_tx_internal.h"
#include "uart.h"
#include "uart_sync.h"

/**
 * @brief UART送信タスクを実行する。
 *
 * @details
 * 送信要求の通知を待ち、送信キューに登録されたメッセージを
 * UARTへ送信する。
 *
 * 1. 送信データ到着イベントを待つ。
 * 2. 送信キューからメッセージを1件取得する。
 * 3. UART送信排他制御用セマフォを取得する。
 * 4. UARTドライバでメッセージを送信する。
 * 5. キューが空になるまで2.から4.を繰り返す。
 * 6. キューが空になったら、次の送信要求を待つ。
 *
 * @param[in] stacd タスク起動コード。現在は使用しない。
 * @param[in] exinf タスク起動時に渡される拡張情報。現在は使用しない。
 */
void task_uarttx(INT stacd, void *exinf)
{
    char text[UART_TX_MESSAGE_SIZE];
    ER err;

    (void)stacd;
    (void)exinf;
 // UART送信排他制御用セマフォを取得する。
    err = uart_sync_lock();
    if(err>=E_OK){
        uart_puts("\r\nuart tx task start\r\n");
        uart_sync_unlock();
    }
    while (TRUE) {
        /* 送信要求が登録されるまで待つ */
        err = uart_tx_wait();
        if (err < E_OK) {
            continue;
        }
        /* キューに入っているメッセージを
         * 空になるまですべて送信する。*/
        while (uart_tx_receive(text) == E_OK) {
            err = uart_sync_lock();
            if (err >= E_OK) {
                uart_puts(text);
                uart_sync_unlock();
            }
        }
    }
}