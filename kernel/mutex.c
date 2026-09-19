/* FIFO mutex: ownership and direct handoff, without priority inheritance. */
#include <trykernel.h>
#include <knldef.h>

static MTXCB mtxcb_tbl[CNF_MAX_MTXID];

/* Called with interrupts disabled. Transfer ownership before making READY. */
static void release_mutex(INT index)
{
    TCB *tcb;
    mtxcb_tbl[index].owner = NULL;
    for(tcb = wait_queue; tcb != NULL; tcb = tcb->next) {
        if(tcb->waifct != TWFCT_MTX || tcb->waiobj != index) continue;
        mtxcb_tbl[index].owner = tcb;
        tqueue_remove_entry(&wait_queue, tcb);
        tcb->state = TS_READY;
        tcb->waifct = TWFCT_NON;
        *tcb->waierr = E_OK;
        tqueue_add_entry(&ready_queue[PRI_INDEX(tcb->itskpri)], tcb);
        break;
    }
}
/*
 * @brief Create a mutex
 * @param pk_cmtx Pointer to the mutex attribute structure
 * @return Mutex ID if successful, otherwise an error code
 */
ID tk_cre_mtx(const T_CMTX *pk_cmtx)
{
    UINT intsts;
    ID index;
    if(is_interrupt_context()) return E_CTX;
    if(pk_cmtx == NULL) return E_PAR;
    if(pk_cmtx->mtxatr != TA_TFIFO) return E_RSATR;
    DI(intsts);
    for(index = 0; index < CNF_MAX_MTXID; index++) {
        if(mtxcb_tbl[index].state == KS_NONEXIST) {
            mtxcb_tbl[index].state = KS_EXIST;
            mtxcb_tbl[index].owner = NULL;
            EI(intsts);
            return index + 1;
        }
    }
    EI(intsts);
    return E_LIMIT;
}

ER tk_loc_mtx(ID mtxid, TMO tmout)
{
    UINT intsts;
    ER err = E_OK;
    MTXCB *mtxcb;
    if(is_interrupt_context() || cur_task == NULL) return E_CTX;
    if(mtxid <= 0 || mtxid > CNF_MAX_MTXID) return E_ID;
    /* Adding the timer period must not overflow signed RELTIM. */
    if(tmout < TMO_FEVR || tmout > INT32_MAX - TIMER_PERIOD) return E_PAR;
    DI(intsts);
    if(intsts != 0U) {
        EI(intsts);
        return E_CTX;
    }
    mtxcb = &mtxcb_tbl[mtxid - 1];
    if(mtxcb->state != KS_EXIST) {
        err = E_NOEXS;
    } else if(mtxcb->owner == cur_task) {
        err = E_ILUSE;
    } else if(mtxcb->owner == NULL) {
        mtxcb->owner = cur_task;
    } else if(tmout == TMO_POL) {
        err = E_TMOUT;
    } else {
        tqueue_remove_entry(&ready_queue[PRI_INDEX(cur_task->itskpri)], cur_task);
        cur_task->state = TS_WAIT;
        cur_task->waifct = TWFCT_MTX;
        cur_task->waiobj = mtxid - 1;
        cur_task->waitim = (tmout == TMO_FEVR) ? tmout : tmout + TIMER_PERIOD;
        cur_task->waierr = &err;
        tqueue_add_entry(&wait_queue, cur_task);
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
        scheduler();
    }
    EI(intsts);
    return err;
}

void mutex_release_all(TCB *owner)
{
    for(INT index = 0; index < CNF_MAX_MTXID; index++) {
        if(mtxcb_tbl[index].state == KS_EXIST && mtxcb_tbl[index].owner == owner) {
            release_mutex(index);
        }
    }
}
