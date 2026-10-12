/**
 * @file    systimer.c
 * @brief   システムタイマ割込みと時間待ちタスクを管理する。
 *
 * @details
 * TryKernelのSysTick割込み処理を実装する。
 *
 * システムタイマ割込みごとにウェイトキューを走査し、
 * 時間待ち状態にあるタスクの残り待ち時間を更新する。
 *
 * 待ち時間が満了したタスクはウェイトキューから削除し、
 * READY状態へ移行して対応するレディキューへ追加する。
 *
 * TMO_FEVRが設定されたタスクは無期限待ちとして扱い、
 * 待ち時間を減算しない。
 *
 * ミューテックス待ちがタイムアウトした場合は、
 * 優先度継承状態を再計算する。
 * tk_dly_tsk()による時間待ちが満了した場合はE_OKを設定し、
 * それ以外の待ち処理がタイムアウトした場合はE_TMOUTを設定する。
 *
 * 待ち時間の更新後は、周期ハンドラを処理するcyclic_tick()と
 * 次に実行するタスクを選択するscheduler()を呼び出す。
 *
 * @note
 * systimer_handler()はSysTick割込みコンテキストで実行される。
 * 割込みコンテキストから待ちAPIを呼び出したり、
 * 長時間ブロックする処理を行ったりしてはならない。
 */

#include <trykernel.h>
#include <knldef.h>

/**
 * @brief 時間待ち状態にあるタスクのウェイトキュー。
 *
 * 時間待ち、イベントフラグ待ち、セマフォ待ち、
 * ミューテックス待ち、メッセージキュー待ちなど、
 * 待ち状態にあるタスクを管理する。
 */
TCB *wait_queue;

/**
 * @brief システムタイマ割込みを処理する。
 *
 * @details
 * ウェイトキューに登録されているタスクを走査し、
 * 各タスクの待ち時間をシステムタイマ周期分だけ更新する。
 *
 * TMO_FEVRが設定されたタスクは無期限待ちとして扱い、
 * 待ち時間を減算しない。
 *
 * 待ち時間が満了したタスクはウェイトキューから削除し、
 * READY状態へ移行して対応するレディキューへ追加する。
 *
 * ミューテックス待ちがタイムアウトした場合は、
 * 優先度継承状態を再計算する。
 * tk_dly_tsk()による時間待ちが満了した場合はE_OKを設定し、
 * それ以外の待ち処理がタイムアウトした場合はE_TMOUTを設定する。
 *
 * 待ち時間の更新後は、周期ハンドラを処理するcyclic_tick()と
 * 次に実行するタスクを選択するscheduler()を呼び出す。
 *
 * @note
 * SysTick割込みコンテキストで実行される。
 * この関数内から待ちAPIを呼び出したり、長時間ブロックする処理を
 * 実行したりしてはならない。
 */
void systimer_handler(void)
{
    TCB     *tcb, *next;

    for( tcb = wait_queue; tcb != NULL; tcb = next) {
        next = tcb->next;
        if(tcb->waitim == TMO_FEVR) {
            continue;
        } else if(tcb->waitim > TIMER_PERIOD) {
            tcb->waitim -= TIMER_PERIOD;  // 待ち時間から経過時間を減じる。
        } else {                          // 待ち時間が経過したタスクを実行できる状態に戻す
            tqueue_remove_entry( &wait_queue, tcb);             // タスクをウェイトキューから外す

            if(tcb->waifct == TWFCT_MTX) {
                mutex_wait_timeout(tcb);
            }

            if(tcb->waifct == TWFCT_DLY) {
                *tcb->waierr = E_OK;         // tk_dly_tskからの復帰はエラー無し
            } else {
                *tcb->waierr = E_TMOUT;      // タイムアウト・エラー
            }

            tcb->state      = TS_READY;
            tcb->waifct     = TWFCT_NON;
            tqueue_add_entry( &ready_queue[PRI_INDEX(tcb->itskpri)], tcb); // タスクをレディキューにつなぐ
        }
    }
    cyclic_tick();
    scheduler();        // スケジューラを実行する
}
