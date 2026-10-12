/**
 * @file    task_sync.c
 * @brief   タスクの遅延、起床待ち、起床要求を管理する。
 *
 * @details
 * タスク付属の同期機能として、タスクの時間待ち、
 * 起床待ち、および起床要求を実装する。
 *
 * tk_dly_tsk()は、指定時間が経過するまで現在のタスクを
 * ウェイトキューへ移して待機させる。
 *
 * tk_slp_tsk()は、起床要求が保留されていない場合に現在のタスクを
 * 起床待ち状態へ移行させる。起床要求がすでに存在する場合は、
 * 要求数を1つ消費して待機せずに復帰する。
 *
 * tk_wup_tsk()は、tk_slp_tsk()で待機中のタスクをREADY状態へ戻す。
 * 対象タスクがすでに実行可能な状態または別の待ち状態の場合は、
 * 起床要求数を増加させる。
 *
 * タスクを待ち状態からREADY状態へ移行した場合は、
 * レディキューへ再登録してスケジューラを実行する。
 *
 * @note
 * tk_dly_tsk()およびtk_slp_tsk()は、割込み・例外コンテキストから
 * 呼び出すことができない。
 *
 * @note
 * タスクの待ち状態とキューを更新する間は割込みを禁止する。
 */
#include <trykernel.h>
#include <knldef.h>
/**
 * @brief 現在のタスクを指定時間だけ遅延させる。
 *
 * @details
 * dlytimが0より大きい場合、現在のタスクをレディキューから削除し、
 * TS_WAIT状態へ移行させてウェイトキューへ登録する。
 *
 * 待ち時間を設定した後、スケジューラを実行して別のREADYタスクへ
 * 処理を切り替える。指定時間が経過すると、システムタイマ処理によって
 * タスクがREADY状態へ戻される。
 *
 * dlytimが0の場合は待機せず、直ちに復帰する。
 *
 * @param[in] dlytim 遅延時間。
 *                   0の場合は遅延処理を行わない。
 *
 * @return ER 処理結果。
 * @retval E_OK  遅延処理が完了した。
 * @retval E_CTX 割込みまたは例外コンテキストから呼び出された。
 *
 * @note
 * 現在のタスクを待ち状態へ移行するため、
 * タスクコンテキストから呼び出す必要がある。
 */
ER tk_dly_tsk( RELTIM dlytim )
{
    UINT    intsts;
    ER      err = E_OK;

    /* 割り込み・例外コンテキストからの待ちは禁止 */
    if(is_interrupt_context()) return E_CTX;

    DI(intsts);     // 割込みの禁止
    if(dlytim > 0) {
        tqueue_remove_top(&ready_queue[PRI_INDEX(cur_task->itskpri)]); // タスクをレディキューから外す

        /* TCBの各種情報を変更する */
        cur_task->state   = TS_WAIT;                // タスクの状態を待ち状態に変更
        cur_task->waifct  = TWFCT_DLY;              // 待ち要因を設定
        cur_task->waitim  = dlytim + TIMER_PERIOD;  // 待ち時間を設定
        cur_task->waierr = &err;                   // 待ち解除時のエラーコード

        tqueue_add_entry(&wait_queue, cur_task);    // タスクをウェイトキューに繋ぐ
        scheduler();                                // スケジューラの実行
    }
    EI(intsts);     // 割込みの許可
    return err;
}
/**
 * @brief 起床要求が発生するまで現在のタスクを待機させる。
 *
 * @details
 * 現在のタスクに保留中の起床要求がある場合は、
 * 起床要求数を1つ減らして待機せずに復帰する。
 *
 * 起床要求がない場合は、現在のタスクをレディキューから削除し、
 * TS_WAIT状態へ移行させてウェイトキューへ登録する。
 * その後、スケジューラを実行して別のREADYタスクへ処理を切り替える。
 *
 * tmoutにTMO_FEVRを指定した場合は無期限に待機する。
 * それ以外の場合は、指定された時間の経過後に待ち状態を解除する。
 *
 * @param[in] tmout 待ち時間。
 *                  TMO_FEVRは無期限待機を示す。
 *
 * @return ER 処理結果。
 * @retval E_OK    起床要求を消費して復帰した、
 *                または起床によって待ち状態が解除された。
 * @retval E_CTX   割込みまたは例外コンテキストから呼び出された。
 * @retval E_TMOUT 待ち時間が満了した。
 *
 * @note
 * 起床要求数はwupcntに保持される。
 * この関数はタスクコンテキストから呼び出す必要がある。
 */
ER tk_slp_tsk( TMO tmout )
{
    UINT    intsts;
    ER      err = E_OK;

    /* 割り込み・例外コンテキストからの待ちは禁止 */
    if(is_interrupt_context()) return E_CTX;

    DI(intsts);     // 割込みの禁止
    if ( cur_task->wupcnt > 0 ) {    // 起床要求有り
        cur_task->wupcnt--;
	} else {                        // 起床要求無し
        tqueue_remove_top(&ready_queue[PRI_INDEX(cur_task->itskpri)]);  // タスクをレディキューから外す

        /* TCBの各種情報を変更する */
        cur_task->state   = TS_WAIT;                // タスクの状態を待ち状態に変更
        cur_task->waifct  = TWFCT_SLP;              // 待ち要因を設定
        cur_task->waitim  = (tmout==TMO_FEVR)?tmout:(tmout+TIMER_PERIOD);   // 待ち時間を設定
        cur_task->waierr = &err;

        tqueue_add_entry(&wait_queue, cur_task);    // タスクをウェイトキューに繋ぐ
        scheduler();                                // スケジューラの実行
    }
    EI(intsts);     // 割込みの許可
    return err;
}
/**
 * @brief 指定されたタスクへ起床要求を送る。
 *
 * @details
 * 指定されたタスクがtk_slp_tsk()による起床待ち状態の場合は、
 * タスクをウェイトキューから削除してREADY状態へ移行し、
 * 対応するレディキューへ登録する。その後、スケジューラを実行する。
 *
 * 対象タスクがREADY状態、または別の待ち状態の場合は、
 * 起床要求数wupcntを1つ増加させる。
 * この要求は、対象タスクが後でtk_slp_tsk()を呼び出した際に消費される。
 *
 * タスクが未登録状態またはDORMANT状態の場合は、
 * 起床要求を送らずE_OBJを返す。
 *
 * @param[in] tskid 起床対象のタスクID。
 *
 * @return ER 処理結果。
 * @retval E_OK    起床処理または起床要求の登録に成功した。
 * @retval E_ID    タスクIDが不正である。
 * @retval E_OBJ   起床要求を受け付けられる状態ではない。
 *
 * @note
 * タスク状態とタスクキューを更新する間は割込みを禁止する。
 */
ER tk_wup_tsk(ID tskid)
{
    TCB	    *tcb;
    UINT    intsts;
    ER      err = E_OK;

    if(tskid <= 0 || tskid > CNF_MAX_TSKID) return E_ID;    /* ID番号チェック */

    DI(intsts);     // 割込みの禁止
    tcb = &tcb_tbl[tskid-1];
    if((tcb->state == TS_WAIT) && (tcb->waifct == TWFCT_SLP)) { // tk_slp_tskで待ち状態か？

        tqueue_remove_entry(&wait_queue, tcb);                  // タスクをウェイトキューから外す

        /* TCBの各種情報を変更する */
        tcb->state	= TS_READY;
        tcb->waifct	= TWFCT_NON;

        tqueue_add_entry(&ready_queue[PRI_INDEX(tcb->itskpri)], tcb);      // タスクをレディキューに繋ぐ
        scheduler();                                            // スケジューラの実行
    } else if(tcb->state == TS_READY || tcb->state == TS_WAIT) {    // 実行できる状態の場合
        tcb->wupcnt++;      // 起床要求数を増やす
    } else {
        err = E_OBJ;       // 起床できるタスクの状態ではない
    }

    EI(intsts);     // 割込みの許可
    return err;
}
