/**
 * @file    task_queue.c
 * @brief   TCBキューの追加・削除処理を実装する。
 *
 * @details
 * タスク管理ブロック（TCB）を単方向または双方向の待ち行列へ
 * 追加・削除するためのキュー操作関数を提供する。
 *
 * キューの先頭はqueueが指し、キューの末尾は先頭TCBのpreメンバーで
 * 管理する。各TCBはpreとnextによって前後のTCBとつながる。
 *
 * tqueue_add_entry()はTCBをキューの末尾へ追加する。
 * tqueue_remove_top()はキューの先頭TCBを削除する。
 * tqueue_remove_entry()は指定されたTCBをキューから削除する。
 *
 * これらの関数は、レディキューやウェイトキューなど、
 * TryKernel内部のタスクキュー操作に使用される。
 *
 * @note
 * 本ファイルの関数はキュー管理情報を直接更新するため、
 * 呼び出し側で必要な割排他制御を行うこと。
 *
 * @note
 * queueはTCBポインタを指すキュー先頭ポインタであり、
 * tcbは有効なTCBを指している必要がある。
 */

#include <trykernel.h>
#include <knldef.h>
/**
 * @brief TCBをタスクキューの末尾へ追加する。
 *
 * @details
 * 指定されたTCBをキューの末尾へ追加する。
 *
 * キューが空の場合は、tcbをキューの先頭として登録し、
 * tcb->preが自身を指すように初期化する。
 *
 * キューに既存のTCBがある場合は、先頭TCBのpreメンバーから
 * キュー末尾を取得し、tcbを末尾へ追加する。
 * 追加後はtcb->nextをNULLに設定する。
 *
 * @param[in,out] queue TCBキューの先頭ポインタ。
 * @param[in]     tcb   追加するTCB。
 *
 * @note
 * tcbは他のキューに登録されていない状態で指定すること。
 * キュー管理情報を更新するため、呼び出し側で必要な排他制御を行うこと。
 */
void tqueue_add_entry(TCB **queue, TCB *tcb)
{
    TCB *queue_end;

    if(*queue == NULL) {    // キューは空なので先頭に追加
        *queue      = tcb;
        tcb->pre    = tcb;
	} else {                // キューの終端に追加
        queue_end       = (*queue)->pre;
        queue_end->next = tcb;
        tcb->pre        = queue_end;
        (*queue)->pre   = tcb;
    }
    tcb->next = NULL;
}
/**
 * @brief タスクキューの先頭TCBを削除する。
 *
 * @details
 * キューが空でない場合、先頭TCBをキューから取り除き、
 * 次のTCBを新しい先頭に設定する。
 *
 * 新しい先頭TCBが存在する場合は、そのpreメンバーに
 * これまでのキュー末尾を設定して、キューのリンクを維持する。
 *
 * キューが空の場合は何も行わない。
 *
 * @param[in,out] queue TCBキューの先頭ポインタ。
 *
 * @note
 * 削除したTCBのpreおよびnextメンバーは初期化しない。
 * 必要な場合は呼び出し側で処理すること。
 */
void tqueue_remove_top(TCB **queue)
{
    TCB	*top;

    if(*queue == NULL) return;  // キューは空

    top = *queue;
    *queue = top->next;
    if(*queue != NULL) {
        (*queue)->pre = top->pre;
    }
}
/**
 * @brief 指定したTCBをタスクキューから削除する。
 *
 * @details
 * 指定されたTCBがキューの先頭にある場合は、
 * tqueue_remove_top()を使用して先頭エントリを削除する。
 *
 * それ以外の場合は、指定TCBの前後にあるTCBのリンクを更新して、
 * キューの途中または末尾から指定TCBを削除する。
 *
 * @param[in,out] queue TCBキューの先頭ポインタ。
 * @param[in]     tcb   削除するTCB。
 *
 * @note
 * tcbは指定されたqueueに登録されている必要がある。
 * queueまたはtcbがNULLの場合、またはtcbがqueueに含まれていない場合の
 * 動作は保証されない。
 *
 * @note
 * 削除したTCBのpreおよびnextメンバーは初期化しない。
 */
void tqueue_remove_entry(TCB **queue, TCB *tcb)
{
    if(*queue == tcb) {     // 指定したエントリはキューの先頭
        tqueue_remove_top(queue);
    } else {                // キューの途中から指定エントリを削除
        (tcb->pre)->next = tcb->next;
        if(tcb->next != NULL) {
            (tcb->next)->pre = tcb->pre;
        } else {
            (*queue)->pre = tcb->pre;
        }
    }
}
