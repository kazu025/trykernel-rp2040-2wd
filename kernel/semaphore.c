/*
 *** Try Kernel
 *      セマフォ
*/

#include <trykernel.h>
#include <knldef.h>

SEMCB   semcb_tbl[CNF_MAX_SEMID];   //!< セマフォ管理ブロック(SEMCB)

/**
 * @brief セマフォを生成する。
 *
 * @details
 * 指定された生成情報をもとにセマフォ管理ブロックを確保し、
 * 初期資源数と最大資源数を設定する。
 *
 * セマフォIDは、未使用の管理ブロックを検索して割り当てる。
 * 管理情報の更新中は割込みを禁止し、処理完了後に元の状態へ戻す。
 *
 * @param[in] pk_csem セマフォ生成情報へのポインタ。
 *                    NULLは指定できない。
 *
 * @return ID 生成されたセマフォIDまたはエラーコード。
 * @retval 正の値 生成されたセマフォID。
 * @retval E_PAR pk_csemがNULL、初期資源数が負、
 *               または最大資源数が不正。
 * @retval E_LIMIT 生成可能なセマフォ数の上限に達している。
 */
ID tk_cre_sem( const T_CSEM *pk_csem )
{
    ID      semid;
    UINT    intsts;
    if(pk_csem == NULL) return E_PAR;
    if(pk_csem->isemcnt < 0) return E_PAR;
    if(pk_csem->maxsem <=0 || pk_csem->maxsem < pk_csem->isemcnt)
        return E_PAR;

    DI(intsts);     // 割込み禁止
    for(semid = 0; semid < CNF_MAX_SEMID && semcb_tbl[semid].state != KS_NONEXIST; semid++);

    if(semid < CNF_MAX_SEMID) {
        /* セマフォ管理情報の初期化 */
        semcb_tbl[semid].state  = KS_EXIST;
        semcb_tbl[semid].semcnt = pk_csem->isemcnt;
        semcb_tbl[semid].maxsem = pk_csem->maxsem;
        semid++;
    } else {
        semid = E_LIMIT;
    }
    EI(intsts);      // 割込み許可
    return semid;
}

/**
 * @brief セマフォの資源を獲得する。
 *
 * @details
 * 指定されたセマフォから、要求された数の資源を取得する。
 *
 * 資源数が足りている場合は、直ちに資源を取得して返る。
 * 資源数が足りず、tmoutにTMO_POLを指定した場合は待機せず、
 * E_TMOUTを返す。
 *
 * TMO_FEVRまたは待ち時間を指定した場合は、資源が解放されるまで
 * 現在のタスクを待ち状態に移行する。待ち状態へ移行した場合は、
 * スケジューラを実行して別のREADYタスクへ処理を切り替える。
 *
 * @param[in] semid セマフォID。
 * @param[in] cnt   獲得する資源数。1以上を指定する。
 * @param[in] tmout 待ち時間。
 *                  TMO_POLは待機なし、
 *                  TMO_FEVRは無期限待機を示す。
 *
 * @return ER 処理結果。
 * @retval E_OK    資源の獲得に成功した。
 * @retval E_CTX   割込みまたは例外コンテキストから呼び出された。
 * @retval E_ID    セマフォIDが不正である。
 * @retval E_NOEXS 指定されたセマフォが存在しない。
 * @retval E_PAR   cntが不正、または要求数がセマフォの最大資源数を
 *                超えている。
 * @retval E_TMOUT 資源を取得できず、待ち時間が満了した。
 *
 * @note
 * セマフォ管理情報を更新する間は割込みを禁止する。
 */
ER tk_wai_sem( ID semid, INT cnt, TMO tmout )
{
    SEMCB   *semcb;
    ER      err = E_OK;
    UINT    intsts;

    /* 割り込み・例外コンテキストからの待ちは禁止 */
    if(is_interrupt_context()) return E_CTX;

    if(semid <= 0 || semid > CNF_MAX_SEMID) return E_ID;
    semcb = &semcb_tbl[--semid];
    if(semcb->state != KS_EXIST) return E_NOEXS;
    if (cnt <=0 || semcb->maxsem < cnt) return E_PAR;

    DI(intsts);     // 割込み禁止
    if(semcb->semcnt >= cnt) {          // 現在のセマフォの資源数 ≧ 要求する資源数
        semcb->semcnt -= cnt;
    } else if(tmout == TMO_POL) {       // 資源が足りなく、かつ、待ち時間0の場合
        err = E_TMOUT;
    } else {                            // 資源が足りなく、待ち状態に移行
        tqueue_remove_top(&ready_queue[PRI_INDEX(cur_task->itskpri)]);     // タスクをレディキューから外す

        /* TCBの各種情報を変更する */
        cur_task->state  = TS_WAIT;      // タスクの状態を待ち状態に変更
        cur_task->waifct = TWFCT_SEM;    // 待ち要因を設定
        cur_task->waiobj = semid;        // 待ちセマフォIDを設定
        cur_task->waitim = ((tmout == TMO_FEVR)? tmout: tmout + TIMER_PERIOD);    // 待ち時間を設定
        cur_task->waisem = cnt;
        cur_task->waierr = &err;

        tqueue_add_entry(&wait_queue, cur_task);     // タスクをウェイトキューに繋ぐ
        scheduler();                                 // スケジューラを実行
    }
    EI(intsts);     // 割込み許可
    return err;
}
/**
 * @brief セマフォへ資源を返却する。
 *
 * @details
 * 指定されたセマフォへ、指定数の資源を返却する。
 *
 * 返却後の資源数が最大資源数を超えない場合は、待ち状態にあるタスクを
 * 確認する。要求している資源数を取得できる待ちタスクがあれば、
 * そのタスクへ資源を割り当ててREADY状態へ移行し、スケジューラを実行する。
 *
 * 返却後の資源数が最大資源数を超える場合は、資源数を元に戻して
 * エラーを返す。
 *
 * @param[in] semid セマフォID。
 * @param[in] cnt   返却する資源数。1以上を指定する。
 *
 * @return ER 処理結果。
 * @retval E_OK    資源の返却に成功した。
 * @retval E_ID    セマフォIDが不正である。
 * @retval E_PAR   cntが0以下である。
 * @retval E_NOEXS 指定されたセマフォが存在しない。
 * @retval E_QOVR 返却後の資源数が最大資源数を超える。
 *
 * @note
 * セマフォ管理情報を更新する間は割込みを禁止する。
 */
ER tk_sig_sem(ID semid, INT cnt)
{
    SEMCB   *semcb;
    TCB     *tcb, *next;
    ER      err = E_OK;
    UINT    intsts;
    if(semid <= 0 || semid > CNF_MAX_SEMID) return E_ID;
    if(cnt <= 0) return E_PAR;

    DI(intsts);     // 割込み禁止
    semcb = &semcb_tbl[--semid];
    if(semcb->state == KS_EXIST) {
        semcb->semcnt += cnt;                                       // 資源の返却
        if(semcb->semcnt <= semcb->maxsem) {
            for( tcb = wait_queue; tcb != NULL; tcb = next) {  // レディキューのタスクを確認
                next = tcb->next; // tqueue_add_entry()で tcb->next= NULLとしているため
                if((tcb->waifct == TWFCT_SEM)&&(tcb->waiobj == semid)) {
                    if( semcb->semcnt >= tcb->waisem) {             // 要求資源数を満たしていれば実行可能状態へ
                        semcb->semcnt -= tcb->waisem;
                        tqueue_remove_entry( &wait_queue, tcb);     // タスクをウェイトキューから外す

                        /* TCBの各種情報を変更する */
                        tcb->state	= TS_READY;
                        tcb->waifct	= TWFCT_NON;
                        *tcb->waierr = E_OK;

                        tqueue_add_entry( &ready_queue[PRI_INDEX(tcb->itskpri)], tcb); // タスクをレディキューへつなぐ
                        scheduler();                                        // スケジューラを実行
                    } else {
                        break;
                    }
                }
            }
        } else {        // 資源数が最大値を超えた
            semcb->semcnt -= cnt;
            err = E_QOVR;
        }
    } else {
        err = E_NOEXS;  // 未登録のセマフォ
    }
    EI(intsts);     // 割込み許可
    return err;
}
