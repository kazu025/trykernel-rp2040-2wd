/**
 * @file    mutex.c
 * @brief   ミューテックスと動的優先度継承を実装する。
 *
 * @details
 * TryKernelのミューテックス生成、ロック、アンロックを管理する。
 *
 * ミューテックスの所有タスクを記録し、所有中のミューテックスを
 * 他のタスクが待っている場合は、待ちタスクの優先度を所有タスクへ
 * 継承させる。
 *
 * TA_TFIFO属性では待ちタスクをFIFO順で扱い、
 * TA_INHERIT属性では待ちタスクの優先度に基づいて所有タスクの
 * 実行優先度を動的に変更する。
 *
 * 優先度継承は、ミューテックスの入れ子や複数タスクの待ち関係にも
 * 対応するため、待ち状態を繰り返し走査して計算する。
 * 優先度が変更されたREADY状態のタスクは、元のレディキューから外し、
 * 新しい優先度に対応するレディキューへ再登録する。
 *
 * ミューテックスが解放された場合は、待ちタスクから次に所有権を
 * 取得するタスクを選択し、そのタスクをREADY状態へ移行する。
 *
 * @note
 * ミューテックスのロックおよびアンロックはタスクコンテキストから
 * 呼び出す必要があり、割込み・例外コンテキストからの呼び出しは
 * 禁止されている。
 *
 * @note
 * 現在の実装では、同じタスクがすでに所有しているミューテックスを
 * 再度ロックする再帰ロックには対応していない。
 *
 * @note
 * ミューテックス管理情報とタスクの優先度を更新する間は、
 * 割込みを禁止して処理する。
 */
#include <trykernel.h>
#include <knldef.h>

/**
 * @brief ミューテックス管理ブロックの配列。
 */
static MTXCB mtxcb_tbl[CNF_MAX_MTXID];

/**
 * @brief 優先度継承計算中の各タスクの優先度。
 */
static PRI calculated_priority[CNF_MAX_TSKID];

/**
 * @brief TCBのアドレスからタスク管理テーブルのインデックスを求める。
 *
 * @details
 * tcbとTCBテーブル先頭アドレスとの差分を計算し、
 * 対応するタスク管理テーブルのインデックスを返す。
 *
 * @param[in] tcb インデックスを求めるタスクのTCB。
 *
 * @return INT tcb_tbl[]内でのTCBのインデックス。
 *
 * @note
 * tcbはtcb_tbl配列内の有効な要素を指している必要がある。
 */
static INT task_index(const TCB *tcb)
{
    return (INT)(tcb - tcb_tbl);
}
/**
 * @brief ミューテックス待ちによるタスク優先度を再計算する。
 *
 * @details
 * すべてのタスクの実行優先度を基準優先度へ戻した後、
 * TA_INHERIT属性のミューテックスと待ちタスクの関係を走査し、
 * 優先度継承を反映した優先度を計算する。
 *
 * ミューテックスを待っているタスクの優先度が、
 * ミューテックス所有者の優先度より高い場合は、
 * 所有者の優先度を待ちタスクに合わせて引き上げる。
 *
 * ミューテックス待ちが入れ子になっている場合にも優先度継承を
 * 伝播させるため、優先度の変更がなくなるまで最大
 * CNF_MAX_TSKID回繰り返し走査する。
 *
 * 計算後、実行優先度が変更されたタスクについては、
 * READY状態であれば現在のレディキューから取り外し、
 * 新しい優先度に対応するレディキューへ再登録する。
 *
 * @note
 * タスク管理情報、ミューテックス管理情報、待ちキューを参照・更新するため、
 * 呼び出し側で割込みを禁止して使用すること。
 *
 * @note
 * TryKernelでは、数値が小さい優先度を高い優先度として扱う。
 */
static void recalculate_priorities(void)
{
    BOOL changed;
    INT i, pass;
    TCB *waiter;
    // 全タスクの優先度を基準優先度に初期化する
    for(i = 0; i < CNF_MAX_TSKID; i++) {
        calculated_priority[i] = tcb_tbl[i].btskpri;
    }

    /* Repeated scans propagate inheritance through nested mutex waits. */
    for(pass = 0; pass < CNF_MAX_TSKID; pass++) {
        changed = FALSE;
        for(i = 0; i < CNF_MAX_MTXID; i++) {
            INT owner_index;
            if(mtxcb_tbl[i].state != KS_EXIST ||
               mtxcb_tbl[i].attr != TA_INHERIT ||
               mtxcb_tbl[i].owner == NULL) {
                continue;
            }
            owner_index = task_index(mtxcb_tbl[i].owner);
            for(waiter = wait_queue; waiter != NULL; waiter = waiter->next) {
                INT waiter_index;
                if(waiter->waifct != TWFCT_MTX || waiter->waiobj != i) continue;
                waiter_index = task_index(waiter);
                // mutexを待っているタスクの優先度が、
                // mutexを所有しているタスクの優先度より高い場合、所有者の優先度を引き上げる
                if(calculated_priority[waiter_index] < calculated_priority[owner_index]) {
                    calculated_priority[owner_index] = calculated_priority[waiter_index];
                    changed = TRUE;
                }
            }
        }
        if(changed == FALSE) break;
    }

    for(i = 0; i < CNF_MAX_TSKID; i++) {
        TCB *tcb = &tcb_tbl[i];
        PRI oldpri;
        if(tcb->state == TS_NONEXIST ||
           tcb->itskpri == calculated_priority[i]) continue;
        // taskの優先度を変更する場合、
        // レディキューから外してから優先度を変更し、再度レディキューに追加する
        oldpri = tcb->itskpri;
        if(tcb->state == TS_READY) {
            tqueue_remove_entry(&ready_queue[PRI_INDEX(oldpri)], tcb);
        }
        tcb->itskpri = calculated_priority[i];
        if(tcb->state == TS_READY) {
            tqueue_add_entry(&ready_queue[PRI_INDEX(tcb->itskpri)], tcb);
        }
    }
}
/**
 * @brief ミューテックスの次の所有候補タスクを選択する。
 *
 * @details
 * ウェイトキューから、指定されたミューテックスを待っているタスクを
 * 検索する。
 *
 * TA_TFIFO属性の場合は、ウェイトキュー上で最初に見つかったタスクを
 * 選択する。TA_INHERIT属性の場合は、待ちタスクの現在の実行優先度を
 *比較し、最も高い優先度のタスクを選択する。
 *
 * TryKernelでは優先度の数値が小さいほど高い優先度を示す。
 *
 * @param[in] index ミューテックス管理テーブルのインデックス。
 *
 * @return TCB* 次にミューテックスの所有権を取得するタスク。
 * @retval NULL 該当する待ちタスクが存在しない。
 *
 * @note
 * indexはmtxcb_tbl[]の0始まりのインデックスである。
 * ウェイトキューを参照するため、呼び出し側で必要に応じて
 * 割込みを禁止して使用すること。
 */
static TCB *select_waiter(INT index)
{
    TCB *tcb;
    TCB *selected = NULL;
    for(tcb = wait_queue; tcb != NULL; tcb = tcb->next) {
        if(tcb->waifct != TWFCT_MTX || tcb->waiobj != index) continue;
        if(selected == NULL ||
           (mtxcb_tbl[index].attr == TA_INHERIT &&
            tcb->itskpri < selected->itskpri)) {
            selected = tcb;
        }
    }
    return selected;
}
/**
 * @brief ミューテックスの所有権を待ちタスクへ移譲する。
 *
 * @details
 * 指定されたミューテックスを待っているタスクをselect_waiter()で選択し、
 * そのタスクへミューテックスの所有権を移す。
 *
 * 待ちタスクが存在する場合は、ウェイトキューから削除してREADY状態へ
 * 移行し、待ち解除結果にE_OKを設定した後、現在の実行優先度に対応する
 * レディキューへ追加する。
 *
 * 待ちタスクが存在しない場合は、ミューテックスの所有者をNULLにする。
 *
 * @param[in] index ミューテックス管理テーブルのインデックス。
 *
 * @note
 * indexはmtxcb_tbl[]の0始まりのインデックスである。
 * ミューテックス管理情報とタスクキューを更新するため、
 * 呼び出し側で割込みを禁止して使用すること。
 */
static void release_mutex(INT index)
{
    TCB *tcb = select_waiter(index);
    mtxcb_tbl[index].owner = tcb;
    if(tcb == NULL) return;
    tqueue_remove_entry(&wait_queue, tcb);
    tcb->state = TS_READY;
    tcb->waifct = TWFCT_NON;
    *tcb->waierr = E_OK;
    tqueue_add_entry(&ready_queue[PRI_INDEX(tcb->itskpri)], tcb);
}
/**
 * @brief ミューテックスを生成する。
 *
 * @details
 * 未使用のミューテックス管理ブロックを検索して確保し、
 * 指定された属性を設定する。
 *
 * 生成直後は所有者を持たない未ロック状態に初期化する。
 * 対応している属性はTA_TFIFOとTA_INHERITである。
 *
 * 管理ブロックの検索と初期化を行う間は割込みを禁止する。
 *
 * @param[in] pk_cmtx ミューテックス生成情報へのポインタ。
 *                    NULLは指定できない。
 *
 * @return ID 生成されたミューテックスIDまたはエラーコード。
 * @retval 正の値 生成されたミューテックスID。
 * @retval E_CTX 割込みまたは例外コンテキストから呼び出された。
 * @retval E_PAR pk_cmtxがNULLである。
 * @retval E_RSATR 未対応のミューテックス属性が指定された。
 * @retval E_LIMIT 生成可能なミューテックス数の上限に達している。
 */
ID tk_cre_mtx(const T_CMTX *pk_cmtx)
{
    UINT intsts;
    ID index;
    if(is_interrupt_context()) return E_CTX;
    if(pk_cmtx == NULL) return E_PAR;
    if(pk_cmtx->mtxatr != TA_TFIFO && pk_cmtx->mtxatr != TA_INHERIT) {
        return E_RSATR;
    }
    DI(intsts);
    for(index = 0; index < CNF_MAX_MTXID; index++) {
        if(mtxcb_tbl[index].state == KS_NONEXIST) {
            mtxcb_tbl[index].state = KS_EXIST;
            mtxcb_tbl[index].attr = pk_cmtx->mtxatr;
            mtxcb_tbl[index].owner = NULL;
            EI(intsts);
            return index + 1;
        }
    }
    EI(intsts);
    return E_LIMIT;
}
/**
 * @brief ミューテックスを取得する。
 *
 * @details
 * 指定されたミューテックスの所有権を現在のタスクが取得する。
 *
 * ミューテックスが未ロックの場合は、現在のタスクを所有者として
 * 直ちに取得する。同じタスクがすでに所有している場合は、
 * 再帰ロックとしてE_ILUSEを返す。
 *
 * ミューテックスが他のタスクによりロックされており、
 * tmoutがTMO_POLの場合は待機せずにE_TMOUTを返す。
 * それ以外の場合は現在のタスクをウェイトキューへ移し、
 * ミューテックスが解放されるまで待機する。
 *
 * TA_INHERIT属性のミューテックスでは、待ち状態へ移行した後に
 * 優先度継承を再計算する。
 *
 * @param[in] mtxid 取得するミューテックスID。
 * @param[in] tmout 待ち時間。
 *                  TMO_POLは待機なし、
 *                  TMO_FEVRは無期限待機を示す。
 *
 * @return ER 処理結果。
 * @retval E_OK    ミューテックスの取得に成功した。
 * @retval E_CTX   割込みまたは例外コンテキストから呼び出された、
 *                または現在のタスクが存在しない。
 * @retval E_ID    ミューテックスIDが不正である。
 * @retval E_PAR   待ち時間が不正である。
 * @retval E_NOEXS 指定されたミューテックスが存在しない。
 * @retval E_ILUSE 現在のタスクがすでにミューテックスを所有している。
 * @retval E_TMOUT ミューテックスを取得できず、待ち時間が満了した。
 *
 * @note
 * ミューテックス管理情報とタスクキューを更新する間は、
 * 割込みを禁止する。
 */
ER tk_loc_mtx(ID mtxid, TMO tmout)
{
    UINT intsts;
    ER err = E_OK;
    MTXCB *mtxcb;
    if(is_interrupt_context() || cur_task == NULL) return E_CTX;
    if(mtxid <= 0 || mtxid > CNF_MAX_MTXID) return E_ID;
    if(tmout < TMO_FEVR || tmout > INT32_MAX - TIMER_PERIOD) return E_PAR;
    DI(intsts);
    if(intsts != 0U) {
        EI(intsts);
        return E_CTX;
    }
    mtxcb = &mtxcb_tbl[mtxid - 1];
    if(mtxcb->state != KS_EXIST) {
        err = E_NOEXS;
    } else if(mtxcb->owner == cur_task) {//
        err = E_ILUSE;
    } else if(mtxcb->owner == NULL) { //オーナーがいない場合、ロックする
        mtxcb->owner = cur_task; // オーナーを設定→mutex取得
    } else if(tmout == TMO_POL) {
        err = E_TMOUT;
    } else {
        // 取得できなかった場合、ウェイトキューに入れる
        tqueue_remove_entry(&ready_queue[PRI_INDEX(cur_task->itskpri)], cur_task);
        cur_task->state = TS_WAIT;
        cur_task->waifct = TWFCT_MTX;
        cur_task->waiobj = mtxid - 1;
        cur_task->waitim = (tmout == TMO_FEVR) ? tmout : tmout + TIMER_PERIOD;
        cur_task->waierr = &err;
        tqueue_add_entry(&wait_queue, cur_task);
        recalculate_priorities();
        scheduler();
    }
    EI(intsts);
    return err;
}
/**
 * @brief ミューテックスを解放する。
 *
 * @details
 * 現在のタスクが所有しているミューテックスを解放する。
 *
 * 待ちタスクが存在する場合は、release_mutex()によって選択された
 * 待ちタスクへ所有権を移譲し、そのタスクをREADY状態へ移行する。
 * その後、優先度継承の状態を再計算し、必要に応じてスケジューラを
 * 実行する。
 *
 * @param[in] mtxid 解放するミューテックスID。
 *
 * @return ER 処理結果。
 * @retval E_OK    ミューテックスの解放に成功した。
 * @retval E_CTX   割込みまたは例外コンテキストから呼び出された、
 *                または現在のタスクが存在しない。
 * @retval E_ID    ミューテックスIDが不正である。
 * @retval E_NOEXS 指定されたミューテックスが存在しない。
 * @retval E_ILUSE 現在のタスクがミューテックスの所有者ではない。
 *
 * @note
 * ミューテックス管理情報とタスクキューを更新する間は、
 * 割込みを禁止する。
 */
ER tk_unl_mtx(ID mtxid)
{
    UINT intsts;
    ER err = E_OK;
    MTXCB *mtxcb;
    if(is_interrupt_context() || cur_task == NULL) return E_CTX;
    if(mtxid <= 0 || mtxid > CNF_MAX_MTXID) return E_ID;
    DI(intsts);
    mtxcb = &mtxcb_tbl[mtxid - 1];
    if(mtxcb->state != KS_EXIST) {
        err = E_NOEXS;
    } else if(mtxcb->owner != cur_task) {
        err = E_ILUSE;
    } else {
        release_mutex(mtxid - 1);
        recalculate_priorities();
        scheduler();
    }
    EI(intsts);
    return err;
}
/**
 * @brief ミューテックス待ちがタイムアウトした後の優先度を再計算する。
 *
 * @details
 * ミューテックスの取得待ちをしていたタスクがタイムアウトした場合に
 * 呼び出される。
 *
 * 待ちタスクがミューテックス待ちから外れたことで、
 * 所有タスクへ継承されていた優先度が不要になる可能性があるため、
 * 全タスクの優先度継承状態を再計算する。
 *
 * @param[in] waiter タイムアウトした待ちタスクのTCB。
 *                   現在の実装では使用しない。
 *
 * @note
 * 優先度継承状態やタスク管理情報を更新するため、
 * 呼び出し側で必要な排他制御を行うこと。
 */
void mutex_wait_timeout(TCB *waiter)
{
    (void)waiter;
    recalculate_priorities();
}
/**
 * @brief 指定されたタスクが所有するミューテックスをすべて解放する。
 *
 * @details
 * ミューテックス管理テーブルを走査し、ownerが所有している
 * ミューテックスをすべて解放する。
 *
 * 待ちタスクが存在するミューテックスについては、
 * release_mutex()により所有権を待ちタスクへ移譲する。
 * すべてのミューテックスを処理した後、優先度継承状態を再計算する。
 *
 * タスク終了処理など、タスクが保持しているミューテックスを
 * 強制的に解放する必要がある場合に使用する。
 *
 * @param[in] owner ミューテックスを所有しているタスクのTCB。
 *
 * @note
 * ミューテックス管理情報と待ちキューを更新するため、
 * 呼び出し側で必要な排他制御を行うこと。
 */
void mutex_release_all(TCB *owner)
{
    INT index;
    for(index = 0; index < CNF_MAX_MTXID; index++) {
        if(mtxcb_tbl[index].state == KS_EXIST && mtxcb_tbl[index].owner == owner) {
            release_mutex(index);
        }
    }
    recalculate_priorities();
}
