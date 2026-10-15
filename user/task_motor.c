/**
 * @file    task_motor.c
 * @brief   モーター出力を周期的に目標値へ近づけるタスクを実装する。
 *
 * @details
 * 周期ハンドラからセマフォでモータータスクへ通知し、タスク側で
 * motor_ramp_step()を呼び出して出力を段階的に更新する。
 */

#include <trykernel.h>
#include "motor.h"
#include "task_motor.h"
#include "uart_tx.h"

/** モーター出力更新周期の通知に使用するセマフォID。 */
static ID motor_tick_semid;

/**
 * @brief 周期通知をモータータスクへ送る。
 * @param[in] exinf 周期ハンドラの拡張情報（この関数では使用しない）。
 * @details 滞留した複数周期分をまとめて1回の通知とし、急な追い付き更新を防ぐ。
 */
static void motor_periodic_handler(void *exinf)
{
    (void)exinf;
    /* 滞留分は1回にまとめ、急な追い付き更新を避ける。 */
    (void)tk_sig_sem(motor_tick_semid, 1);
}

/**
 * @brief モーター更新用セマフォと50 ms周期ハンドラを初期化・開始する。
 * @retval E_OK 周期ハンドラの開始に成功した。
 * @return セマフォまたは周期ハンドラの作成・開始に失敗した場合は、
 *         TRYカーネルのエラーコード。
 */
ER task_motor_init(void)
{
    static const T_CSEM csem = { .sematr = TA_TFIFO, .isemcnt = 0, .maxsem = 1 };
    static const T_CCYC ccyc = {
        .exinf = NULL,
        .cycatr = TA_HLNG, .cychdr = motor_periodic_handler,
        .cyctim = 50, .cycphs = 50,
    };
    ID cycid;
    ER err;
    motor_tick_semid = tk_cre_sem(&csem);
    if(motor_tick_semid < E_OK) return (ER)motor_tick_semid;
    cycid = tk_cre_cyc(&ccyc);
    if(cycid < E_OK){
        return (ER)cycid;
    }
    err = tk_sta_cyc(cycid);
    if(err < E_OK){
        (void)tk_del_cyc(cycid);
    }
    return err;
}

/**
 * @brief 周期通知を待ち、モーター出力のランプ更新を実行するタスク。
 * @param[in] stacd タスク起動コード（この関数では使用しない）。
 * @param[in] exinf タスク拡張情報（この関数では使用しない）。
 * @details セマフォ通知を受け取るたびにmotor_ramp_step()を呼び出す。
 */
void task_motor(INT stacd, void *exinf)
{
    (void)stacd;
    (void)exinf;
    while(1){
        if(tk_wai_sem(motor_tick_semid, 1, TMO_FEVR) == E_OK) motor_ramp_step();
    }
}
