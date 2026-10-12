/**
 * @file    message_queue.c
 * @brief   固定長メッセージキューを実装する。
 *
 * @details
 * 固定長メッセージキューの生成、メッセージ送信、
 * メッセージ受信を管理する。
 *
 * メッセージキューはFIFO方式でメッセージを格納し、
 * 送信側と受信側のタスク間で固定サイズのデータを受け渡す。
 *
 * キューに空きがない状態で送信要求が発生した場合、
 * 送信タスクを送信待ち状態へ移行する。
 * キューが空の状態で受信要求が発生した場合は、
 * 受信タスクを受信待ち状態へ移行する。
 *
 * 送信待ちタスクと受信待ちタスクが存在する場合は、
 * キューを経由せずにメッセージを直接受け渡す。
 *
 * 待ち時間としてTMO_POLが指定された場合は待機せず、
 * TMO_FEVRが指定された場合は無期限に待機する。
 * それ以外の値が指定された場合は、指定時間経過後に
 * 待ち状態を解除する。
 *
 * キュー管理情報の更新中は割込みを禁止し、
 * タスクをWAIT状態またはREADY状態へ移行する際には
 * スケジューラを実行する。
 *
 * @note
 * tk_snd_msgq()およびtk_rcv_msgq()はタスクコンテキストから
 * 呼び出す必要があり、割込み・例外コンテキストからの呼び出しは
 * 禁止されている。
 *
 * @note
 * メッセージは固定長で扱われるため、送受信に使用するバッファは
 * 生成時に指定したメッセージサイズを満たしている必要がある。
 */


#include <trykernel.h>
#include <knldef.h>

/**
 * @brief 固定長メッセージキュー管理ブロックの配列。
 *
 * 各メッセージキューの状態、バッファ、
 * メッセージサイズ、格納数、読み書き位置を保持する。
 */
MSGQCB msgqcb_tbl[CNF_MAX_MSGQID];

/**
 * @brief 固定サイズのメッセージをバイト単位でコピーする。
 *
 * @details
 * srcが指すメッセージからsizeバイトを読み出し、
 * dstが指す領域へ順番にコピーする。
 *
 * メッセージキュー内の送受信データを、
 * 指定されたメッセージサイズ単位で複製するために使用する。
 *
 * @param[out] dst コピー先バッファ。
 * @param[in]  src コピー元バッファ。
 * @param[in]  size コピーするバイト数。
 *
 * @note
 * dstとsrcは、sizeバイト以上の有効な領域を指している必要がある。
 * この関数はNULLポインタやサイズの妥当性を検査しない。
 */
static void copy_message(void *dst, const void *src, INT size)
{
    UB *d = (UB *)dst;
    const UB *s = (const UB *)src;

    while(size-- > 0) {
        *d++ = *s++;
    }
}

/**
 * @brief 指定されたメッセージキューを待っているタスクを検索する。
 *
 * @details
 * ウェイトキューを先頭から走査し、待ち要因と待ち対象のメッセージキューIDが
 * 一致する最初のタスクを返す。
 *
 * 該当するタスクが存在しない場合はNULLを返す。
 *
 * @param[in] waifct 検索対象の待ち要因。
 *                   送信待ちまたは受信待ちを指定する。
 * @param[in] msgqid 検索対象のメッセージキューID。
 *
 * @return TCB* 該当するタスクのTCB。
 * @retval NULL 該当する待ちタスクが存在しない。
 *
 * @note
 * ウェイトキューを参照するため、呼び出し側で必要に応じて
 * 割込みを禁止して使用すること。
 */
static TCB *find_waiting_task(TWFCT waifct, ID msgqid)
{
    TCB *tcb;

    for(tcb = wait_queue; tcb != NULL; tcb = tcb->next) {
        if((tcb->waifct == waifct) && (tcb->waiobj == msgqid)) {
            return tcb;
        }
    }
    return NULL;
}

/**
 * @brief 待ち状態のタスクをREADY状態へ移行する。
 *
 * @details
 * 指定されたTCBをウェイトキューから削除し、
 * タスク状態をTS_READYへ変更する。
 *
 * 待ち要因を解除し、待ち処理の結果としてE_OKを設定した後、
 * タスクの実行優先度に対応するレディキューへ追加する。
 *
 * @param[in,out] tcb READY状態へ移行するタスクのTCB。
 *
 * @note
 * tcbは事前にウェイトキューへ登録されている必要がある。
 * ウェイトキューとレディキューを更新するため、
 * 呼び出し側で割込みを禁止して使用すること。
 */
static void make_ready(TCB *tcb)
{
    tqueue_remove_entry(&wait_queue, tcb);
    tcb->state = TS_READY;
    tcb->waifct = TWFCT_NON;
    *tcb->waierr = E_OK;
    tqueue_add_entry(&ready_queue[PRI_INDEX(tcb->itskpri)], tcb);
}
/**
 * @brief メッセージキューへメッセージを追加する。
 *
 * @details
 * メッセージキューのtailが示す位置へ、msgqcb->msgszバイトの
 * メッセージをコピーする。
 *
 * メッセージを格納した後、tailを次の位置へ進める。
 * tailがキュー末尾に到達した場合は0へ戻し、
 * リングバッファとして循環させる。
 *
 * @param[in,out] msgqcb メッセージキュー管理ブロック。
 * @param[in]     msg    追加するメッセージへのポインタ。
 *
 * @note
 * 呼び出し前にメッセージキューに空きがあることを確認する必要がある。
 * msgqcbおよびmsgは有効な領域を指していなければならない。
 * キュー管理情報を更新するため、呼び出し側で排他制御を行うこと。
 */
static void enqueue_message(MSGQCB *msgqcb, const void *msg)
{
    UB *dst = msgqcb->buffer + (msgqcb->tail * msgqcb->msgsz);

    copy_message(dst, msg, msgqcb->msgsz);
    msgqcb->tail++;
    if(msgqcb->tail >= msgqcb->maxmsg) {
        msgqcb->tail = 0;
    }
    msgqcb->count++;
}
/**
 * @brief メッセージキューからメッセージを1件取り出す。
 *
 * @details
 * メッセージキューのheadが示す位置から、
 * msgqcb->msgszバイトのメッセージをmsgへコピーする。
 *
 * メッセージを取り出した後、headを次の位置へ進める。
 * headがキュー末尾に到達した場合は0へ戻し、
 * リングバッファとして循環させる。
 *
 * @param[in,out] msgqcb メッセージキュー管理ブロック。
 * @param[out]    msg    取り出したメッセージの格納先。
 *
 * @note
 * 呼び出し前にメッセージキューに少なくとも1件のメッセージが
 * 格納されている必要がある。
 * msgqcbおよびmsgは有効な領域を指していなければならない。
 * キュー管理情報を更新するため、呼び出し側で排他制御を行うこと。
 */
static void dequeue_message(MSGQCB *msgqcb, void *msg)
{
    UB *src = msgqcb->buffer + (msgqcb->head * msgqcb->msgsz);

    copy_message(msg, src, msgqcb->msgsz);
    msgqcb->head++;
    if(msgqcb->head >= msgqcb->maxmsg) {
        msgqcb->head = 0;
    }
    msgqcb->count--;
}
/**
 * @brief 固定長メッセージキューを生成する。
 *
 * @details
 * 指定された生成情報を検証し、未使用のメッセージキュー管理ブロックを
 * 確保して初期化する。
 *
 * メッセージ格納用バッファは呼び出し側が用意した領域を使用し、
 * メッセージサイズ、最大メッセージ数、読み出し位置、
 * 書き込み位置、および現在の格納数を設定する。
 *
 * 現在はFIFO属性（TA_TFIFO）のメッセージキューだけを生成できる。
 * 管理ブロックの検索と初期化を行う間は割込みを禁止する。
 *
 * @param[in] pk_cmsgq メッセージキュー生成情報へのポインタ。
 *                     NULLは指定できない。
 *
 * @return ID 生成されたメッセージキューIDまたはエラーコード。
 * @retval 正の値 生成されたメッセージキューID。
 * @retval E_PAR pk_cmsgq、メッセージバッファがNULL、
 *               メッセージサイズまたは最大メッセージ数が不正である。
 * @retval E_RSATR FIFO以外の未対応属性が指定された。
 * @retval E_LIMIT 生成可能なメッセージキュー数の上限に達している。
 *
 * @note
 * メッセージ格納用バッファの確保と管理は呼び出し側の責任で行う。
 */
ID tk_cre_msgq(const T_CMSGQ *pk_cmsgq)
{
    ID msgqid;
    UINT intsts;

    if(pk_cmsgq == NULL) return E_PAR;
    if(pk_cmsgq->bufptr == NULL) return E_PAR;
    if((pk_cmsgq->msgsz <= 0) || (pk_cmsgq->maxmsg <= 0)) return E_PAR;
    if(pk_cmsgq->msgqatr != TA_TFIFO) return E_RSATR;

    DI(intsts);
    for(msgqid = 0;
        (msgqid < CNF_MAX_MSGQID) &&
        (msgqcb_tbl[msgqid].state != KS_NONEXIST);
        msgqid++);

    if(msgqid < CNF_MAX_MSGQID) {
        MSGQCB *msgqcb = &msgqcb_tbl[msgqid];
        msgqcb->state = KS_EXIST;
        msgqcb->buffer = (UB *)pk_cmsgq->bufptr;
        msgqcb->msgsz = pk_cmsgq->msgsz;
        msgqcb->maxmsg = pk_cmsgq->maxmsg;
        msgqcb->count = 0;
        msgqcb->head = 0;
        msgqcb->tail = 0;
        msgqid++;
    } else {
        msgqid = E_LIMIT;
    }
    EI(intsts);
    return msgqid;
}
/**
 * @brief メッセージキューへメッセージを送信する。
 *
 * @details
 * 指定されたメッセージキューへ、メッセージを1件送信する。
 *
 * 受信待ちのタスクが存在する場合は、キューを経由せずに
 * そのタスクの受信バッファへ直接メッセージをコピーし、
 * 待ち状態を解除する。
 *
 * 受信待ちタスクがなく、キューに空きがある場合は、
 * メッセージをキューへ格納する。
 *
 * キューが満杯でTMO_POLが指定された場合は待機せず、
 * E_TMOUTを返す。それ以外の場合は、送信タスクを送信待ち状態へ移行し、
 * 空きができるまで待機する。
 *
 * @param[in] msgqid メッセージキューID。
 * @param[in] msg    送信するメッセージへのポインタ。
 * @param[in] tmout  待ち時間。
 *                   TMO_POLは待機なし、
 *                   TMO_FEVRは無期限待機を示す。
 *
 * @return ER 処理結果。
 * @retval E_OK    メッセージの送信に成功した。
 * @retval E_CTX   割込みまたは例外コンテキストから呼び出された。
 * @retval E_ID    メッセージキューIDが不正である。
 * @retval E_PAR   msgがNULL、または待ち時間が不正である。
 * @retval E_NOEXS 指定されたメッセージキューが存在しない。
 * @retval E_TMOUT キューが満杯で、待ち時間が満了した。
 *
 * @note
 * メッセージキューが満杯で待ち状態へ移行する場合、
 * msgが指すメッセージは送信完了まで有効でなければならない。
 */
ER tk_snd_msgq(ID msgqid, const void *msg, TMO tmout)
{
    MSGQCB *msgqcb;
    TCB *receiver;
    ER err = E_OK;
    UINT intsts;

    if(is_interrupt_context()) return E_CTX;
    if((msgqid <= 0) || (msgqid > CNF_MAX_MSGQID)) return E_ID;
    if((msg == NULL) || (tmout < TMO_FEVR)) return E_PAR;

    DI(intsts);
    msgqcb = &msgqcb_tbl[--msgqid];
    if(msgqcb->state != KS_EXIST) {
        err = E_NOEXS;
    } else {
        receiver = find_waiting_task(TWFCT_RCV_MSGQ, msgqid);
        if(receiver != NULL) {
            copy_message(receiver->rcvmsg, msg, msgqcb->msgsz);
            make_ready(receiver);
            scheduler();
        } else if(msgqcb->count < msgqcb->maxmsg) {
            enqueue_message(msgqcb, msg);
        } else if(tmout == TMO_POL) {
            err = E_TMOUT;
        } else {
            tqueue_remove_top(&ready_queue[PRI_INDEX(cur_task->itskpri)]);
            cur_task->state = TS_WAIT;
            cur_task->waifct = TWFCT_SND_MSGQ;
            cur_task->waiobj = msgqid;
            cur_task->waitim = (tmout == TMO_FEVR) ? tmout : tmout + TIMER_PERIOD;
            cur_task->sndmsg = msg;
            cur_task->waierr = &err;
            tqueue_add_entry(&wait_queue, cur_task);
            scheduler();
        }
    }
    EI(intsts);
    return err;
}
/**
 * @brief メッセージキューからメッセージを受信する。
 *
 * @details
 * 指定されたメッセージキューから、メッセージを1件受信する。
 *
 * キューにメッセージが存在する場合は、先頭のメッセージをmsgへ
 * コピーする。メッセージを取り出したことでキューに空きができた場合は、
 * 送信待ちのタスクがあれば、そのタスクのメッセージをキューへ格納して
 * 待ち状態を解除する。
 *
 * キューが空でTMO_POLが指定された場合は待機せず、
 * E_TMOUTを返す。それ以外の場合は、受信タスクを受信待ち状態へ移行し、
 * メッセージが送信されるまで待機する。
 *
 * @param[in]  msgqid メッセージキューID。
 * @param[out] msg    受信したメッセージを格納するバッファ。
 * @param[in]  tmout  待ち時間。
 *                   TMO_POLは待機なし、
 *                   TMO_FEVRは無期限待機を示す。
 *
 * @return ER 処理結果。
 * @retval E_OK    メッセージの受信に成功した。
 * @retval E_CTX   割込みまたは例外コンテキストから呼び出された。
 * @retval E_ID    メッセージキューIDが不正である。
 * @retval E_PAR   msgがNULL、または待ち時間が不正である。
 * @retval E_NOEXS 指定されたメッセージキューが存在しない。
 * @retval E_TMOUT キューが空で、待ち時間が満了した。
 *
 * @note
 * メッセージキューが空で待ち状態へ移行する場合、
 * msgが指す受信バッファは受信完了まで有効でなければならない。
 */
ER tk_rcv_msgq(ID msgqid, void *msg, TMO tmout)
{
    MSGQCB *msgqcb;
    TCB *sender;
    ER err = E_OK;
    UINT intsts;

    if(is_interrupt_context()) return E_CTX;
    if((msgqid <= 0) || (msgqid > CNF_MAX_MSGQID)) return E_ID;
    if((msg == NULL) || (tmout < TMO_FEVR)) return E_PAR;

    DI(intsts);
    msgqcb = &msgqcb_tbl[--msgqid];
    if(msgqcb->state != KS_EXIST) {
        err = E_NOEXS;
    } else if(msgqcb->count > 0) {
        dequeue_message(msgqcb, msg);

        /* 空いた1件へ、最初の送信待ちタスクのメッセージを格納する */
        sender = find_waiting_task(TWFCT_SND_MSGQ, msgqid);
        if(sender != NULL) {
            enqueue_message(msgqcb, sender->sndmsg);
            make_ready(sender);
            scheduler();
        }
    } else if(tmout == TMO_POL) {
        err = E_TMOUT;
    } else {
        tqueue_remove_top(&ready_queue[PRI_INDEX(cur_task->itskpri)]);
        cur_task->state = TS_WAIT;
        cur_task->waifct = TWFCT_RCV_MSGQ;
        cur_task->waiobj = msgqid;
        cur_task->waitim = (tmout == TMO_FEVR) ? tmout : tmout + TIMER_PERIOD;
        cur_task->rcvmsg = msg;
        cur_task->waierr = &err;
        tqueue_add_entry(&wait_queue, cur_task);
        scheduler();
    }
    EI(intsts);
    return err;
}
