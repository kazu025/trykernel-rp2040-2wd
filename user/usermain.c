/**
 * @file    usermain.c
 * @brief   TryKernel 2WDの初期化とユーザータスクの起動を行う。
 *
 * @details
 * モーター、LED、I2C、UARTおよび関連タスクを初期化し、UART送受信、
 * 距離測定、モーター制御などのタスクを生成して起動する。
 * 初期化またはタスク起動でエラーが発生した場合は、そのエラーコードを返す。
 */

#include <trykernel.h>
#include "gpio.h"
#include "motor.h"
#include "i2c.h"
#include "uart_sync.h"
#include "uart_tx.h"
#include "task_uartrx.h"
#include "task_distance.h"
#include "task_motor.h"

extern void task_uarttx(INT stacd, void *exinf);
extern void task_led1(INT stacd, void *exinf);

static UW stack_tx[1024 / sizeof(UW)];       //!< UART送信タスクのスタック領域。
static UW stack_rx[1024 / sizeof(UW)];       //!< UART受信タスクのスタック領域。
static UW stack_led[1024 / sizeof(UW)];      //!< LED制御タスクのスタック領域。
static UW stack_distance[1024 / sizeof(UW)]; //!< 距離測定タスクのスタック領域。
static UW stack_motor[1024 / sizeof(UW)];    //!< モーター制御タスクのスタック領域。

/**
 * @brief 指定されたエントリー関数からタスクを生成して起動する。
 * @param[in] entry タスクのエントリー関数。
 * @param[in] priority タスクの初期優先度。
 * @param[in] stack タスクスタックとして使用するメモリー領域。
 * @param[in] size スタック領域のサイズ（バイト）。
 * @return タスク生成または起動の結果を示すTRYカーネルのエラーコード。
 * @retval E_OK タスクを正常に起動した。
 */
static ER start_task(FP entry, PRI priority, UW *stack, SZ size)
{
    T_CTSK config = {
        .tskatr = TA_HLNG | TA_RNG3 | TA_USERBUF,
        .task = entry,
        .itskpri = priority,
        .stksz = size,
        .bufptr = stack,
    };
    ID id = tk_cre_tsk(&config);
    if(id < E_OK) return (ER)id;
    return tk_sta_tsk(id, 0);
}

/**
 * @brief TryKernel 2WDの周辺機器を初期化し、必要なユーザータスクを起動する。
 * @return 初期化またはタスク起動の結果を示すエラーコード。
 * @retval 0 すべての初期化とタスク起動に成功した。
 * @details UART、LED、距離測定、モーター制御を含む2WD構成を起動する。
 *          初期化やタスク起動でエラーが発生した場合は、その時点で処理を終了する。
 */
int usermain(void)
{
    ER err;
    motor_init();
    led25_init();
    i2c0_init();
    err = i2c0_sync_init();
    if(err < E_OK) return err;
    err = uart_sync_init();
    if(err < E_OK) return err;
    err = uart_tx_init();
    if(err < E_OK) return err;
    err = task_uartrx_init();
    if(err < E_OK) return err;
    err = start_task((FP)task_uarttx, 6, stack_tx, sizeof(stack_tx));
    if(err < E_OK) return err;
    uart_tx_send("TryKernel 2WD: UART / LED / motor ready\r\n");
    err = start_task((FP)task_led1, 12, stack_led, sizeof(stack_led));
    if(err < E_OK) return err;
    err = start_task((FP)task_distance, 10,
                     stack_distance, sizeof(stack_distance));
    if(err < E_OK) return err;
    err = task_motor_init();
    if(err < E_OK) return err;
    err = start_task((FP)task_motor, 8, stack_motor, sizeof(stack_motor));
    if(err < E_OK) return err;
    return start_task((FP)task_uartrx, 4, stack_rx, sizeof(stack_rx));
}
