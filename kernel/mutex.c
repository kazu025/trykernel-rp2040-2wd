/* Mutex with strict, dynamic priority inheritance. */
#include <trykernel.h>
#include <knldef.h>

static MTXCB mtxcb_tbl[CNF_MAX_MTXID];
static PRI calculated_priority[CNF_MAX_TSKID];

static INT task_index(const TCB *tcb)
{
    return (INT)(tcb - tcb_tbl);
}

/* Recalculate priorities from the base priorities and the mutex wait graph. */
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

/* FIFO for TA_TFIFO; highest current priority for TA_INHERIT. */
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

/* Transfer ownership before making the selected waiter READY. */
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
/*
 * mutex生成
 * 属性: TA_TFIFO,TA_INHERITのみ有効
 * mtxid: 生成されたmutexのIDを返す
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
/*
 * mutex取得
 * mtxid: 取得するmutexのID
 * tmout: タイムアウト時間
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

void mutex_wait_timeout(TCB *waiter)
{
    (void)waiter;
    recalculate_priorities();
}

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
