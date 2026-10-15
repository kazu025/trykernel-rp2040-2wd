/**
 * @file    task_uartrx.c
 * @brief   UART受信割り込みから文字を受け取り、コンソールへ渡す。
 *
 * @details
 * UART受信割り込みからイベントフラグで通知を受け、受信リングバッファに
 * 残っている文字をすべて取り出してコンソール入力処理へ渡す。
 * コンソールの初期化、UART受信割り込みの有効化、起動メッセージと
 * プロンプトの表示も行う。
 */

#include <trykernel.h>
#include "uart.h"
#include "console.h"
#include "uart_tx.h"
#include "task_uartrx.h"
/*
 1文字受信
  ↓
UARTハードウェアFIFO
  ↓
UART受信割込み
  ↓                             ------------------------------------
uart0_irq_handler()                      uart.c
  ↓
ソフトウェア受信リングバッファ
  ↓                             ------------------------------------
tk_set_flg()  <= コールバック関数 uart_rx_notify_from_isr()
  ↓                                 task_uarttx.c
task_uartrx()
  ↓
uart_rx_getc()
  ↓
console_input_char()               ----------------------------------
  ↓
console_line_bufへ格納
  ↓
'\r'を受信
  ↓
command_execute()
  ↓
コマンド実行
 */
#define UART_RX_EVENT_DATA (1U << 0) //!< UART受信データ到着を示すイベントフラグ。
static ID uart_rx_flgid; //!< UART受信タスク起床用イベントフラグID
/**
 * @brief UART受信割り込みから受信タスクへデータ到着を通知する。
 *
 * @details
 * UART受信割込みハンドラから呼び出され、受信イベントを示す
 * イベントフラグをセットする。これにより、イベントフラグを
 * 待っているUART受信タスクをREADY状態へ移行させる。
 *
 * @note
 * 割込みコンテキストから呼び出されるため、待ちを伴う処理や
 * 時間のかかる処理を行わないこと。
 *
 * @note
 * UART受信イベントフラグが未生成の場合は何も行わない。
 */
static void uart_rx_notify_from_isr(void){
    if(uart_rx_flgid > 0){
        (void)tk_set_flg(uart_rx_flgid, UART_RX_EVENT_DATA);
    }
}
/**
 * @brief UART受信タスク関連のオブジェクトを初期化する。
 *
 * @details
 * UART受信タスクが使用するイベントフラグを生成し、
 * UART受信割込みからの通知関数を登録する。
 *
 * イベントフラグの初期状態は、受信イベントなし（0）に設定する。
 *
 * @return 初期化結果を示すTRYカーネルのエラーコード。
 * @retval E_OK 初期化に成功した。
 * @retval E_LIMIT イベントフラグを生成できなかった。
 * @retval E_PAR イベントフラグの設定に問題があった。
 *
 * @note
 * この関数はUART受信タスクを起動する前に呼び出すこと。
 */
ER task_uartrx_init(void){
    T_CFLG cflg;
    cflg.flgatr = TA_TFIFO;
    cflg.iflgptn = 0U;

    uart_rx_flgid = tk_cre_flg(&cflg);
    if(uart_rx_flgid < E_OK){
        return (ER)uart_rx_flgid;
    }
    uart_rx_set_notify(uart_rx_notify_from_isr);
    return E_OK;
}
/**
 * @brief UART受信データをコンソールへ渡すタスクを実行する。
 *
 * @details
 * UART受信に必要なバッファ、コンソール、UART受信割込みを初期化した後、
 * UART受信タスクの起動メッセージとコンソールプロンプトを送信する。
 *
 * 初期化後は、UART受信割込みからイベントフラグが通知されるまで待機する。
 * 通知を受けると、ソフトウェア受信リングバッファが空になるまで
 * 受信データを1バイトずつ取り出し、console_input_char()へ渡す。
 *
 * イベントフラグの待ちに失敗した場合は、エラーを処理せず再び待機する。
 *
 * @param[in] stacd タスク起動コード。現在は使用しない。
 * @param[in] exinf タスク起動時に渡される拡張情報。現在は使用しない。
 *
 * @note
 * 本タスクは無限ループで動作し、通常は関数から戻らない。
 */
void task_uartrx(INT stacd, void *exinf)
{
    UB c;
    ER err;
    UINT flgptn;

    (void)stacd;
    (void)exinf;

    uart_rxbuf_init();
    console_init();
    uart_rx_irq_enable();

    uart_tx_send("\r\nuart rx task start\r\n");
    console_prompt();

    while (1) {
        /* UART割り込みから受信通知が来るまで待つ */
        err = tk_wai_flg(uart_rx_flgid, UART_RX_EVENT_DATA, TWF_ORW | TWF_BITCLR, &flgptn, TMO_FEVR);
        if(err < E_OK){
            continue;
        }
        while (uart_rx_getc(&c)) {
            console_input_char(c);
        }
    }
}
