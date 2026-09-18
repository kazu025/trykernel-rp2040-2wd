#include <trykernel.h>
#include <knldef.h>
#include <assert.h>
#include <stdio.h>
#include <string.h>

UINT host_primask;
BOOL host_interrupt;
TCB *cur_task, *sche_task, *ready_queue[CNF_MAX_TSKPRI];
static void (*schedule_hook)(void);
static ID mutex_id;
#define A (&tcb_tbl[0])
#define B (&tcb_tbl[1])
#define C (&tcb_tbl[2])

void *make_context(UW *sp, UINT size, void (*fp)())
{ (void)size; (void)fp; return sp; }
void scheduler(void)
{
    void (*hook)(void) = schedule_hook;
    schedule_hook = NULL;
    if(hook != NULL) hook();
}
static void select_task(TCB *task) { cur_task = task; host_primask = 0; }
static void reset_tasks(void)
{
    memset(tcb_tbl, 0, sizeof(TCB) * CNF_MAX_TSKID);
    memset(ready_queue, 0, sizeof(ready_queue));
    wait_queue = NULL;
    for(INT i = 0; i < 3; i++) {
        tcb_tbl[i].itskpri = i + 1;
        tcb_tbl[i].state = TS_READY;
        tqueue_add_entry(&ready_queue[i], &tcb_tbl[i]);
    }
    select_task(A);
}
static void timeout_hook(void)
{
    assert(B->state == TS_WAIT && B->waifct == TWFCT_MTX);
    assert(ready_queue[1] == NULL);
    systimer_handler();
    assert(B->state == TS_WAIT);
    systimer_handler();
    assert(B->state == TS_READY && wait_queue == NULL);
}
static void release_a(void)
{
    select_task(A);
    assert(tk_unl_mtx(mutex_id) == E_OK);
    assert(B->state == TS_READY);
    assert(tk_loc_mtx(mutex_id, TMO_POL) == E_TMOUT); /* no barging */
    select_task(B);
}
static void release_fifo(void)
{
    assert(wait_queue == B && B->next == C);
    release_a();
    assert(C->state == TS_WAIT);
    assert(tk_unl_mtx(mutex_id) == E_OK);
    assert(C->state == TS_READY && wait_queue == NULL);
    select_task(C);
}
static void queue_c(void)
{
    select_task(C);
    schedule_hook = release_fifo;
    assert(tk_loc_mtx(mutex_id, TMO_FEVR) == E_OK);
    assert(tk_unl_mtx(mutex_id) == E_OK);
    select_task(B);
}
static ID second_id;
static void exit_a(void)
{
    select_task(A);
    tk_ext_tsk();
    assert(A->state == TS_DORMANT && ready_queue[0] == NULL);
    select_task(B);
    assert(tk_loc_mtx(second_id, TMO_POL) == E_OK);
    assert(tk_unl_mtx(second_id) == E_OK);
}
int main(void)
{
    T_CMTX attr = {TA_TFIFO};
    T_CMTX bad = {TA_TPRI};
    assert(tk_cre_mtx(NULL) == E_PAR);
    assert(tk_cre_mtx(&bad) == E_RSATR);
    host_interrupt = TRUE;
    assert(tk_cre_mtx(&attr) == E_CTX);
    host_interrupt = FALSE;
    mutex_id = tk_cre_mtx(&attr);
    assert(mutex_id == 1);
    reset_tasks();
    assert(tk_loc_mtx(0, 0) == E_ID);
    assert(tk_unl_mtx(CNF_MAX_MTXID + 1) == E_ID);
    assert(tk_loc_mtx(2, 0) == E_NOEXS);
    assert(tk_unl_mtx(2) == E_NOEXS);
    assert(tk_loc_mtx(mutex_id, -2) == E_PAR);
    assert(tk_loc_mtx(mutex_id, INT32_MAX) == E_PAR);
    assert(tk_unl_mtx(mutex_id) == E_ILUSE);
    host_primask = 1;
    assert(tk_loc_mtx(mutex_id, 0) == E_CTX && host_primask == 1);
    host_primask = 0;
    host_interrupt = TRUE;
    assert(tk_loc_mtx(mutex_id, 0) == E_CTX);
    assert(tk_unl_mtx(mutex_id) == E_CTX);
    host_interrupt = FALSE;
    cur_task = NULL;
    assert(tk_loc_mtx(mutex_id, 0) == E_CTX);
    assert(tk_unl_mtx(mutex_id) == E_CTX);
    select_task(A);
    assert(tk_loc_mtx(mutex_id, 0) == E_OK);
    assert(tk_loc_mtx(mutex_id, 0) == E_ILUSE);
    select_task(B);
    assert(tk_unl_mtx(mutex_id) == E_ILUSE);
    assert(tk_loc_mtx(mutex_id, 0) == E_TMOUT);
    schedule_hook = timeout_hook;
    assert(tk_loc_mtx(mutex_id, TIMER_PERIOD) == E_TMOUT);
    select_task(A);
    assert(tk_unl_mtx(mutex_id) == E_OK);
    select_task(B);
    assert(tk_loc_mtx(mutex_id, 0) == E_OK);
    assert(tk_unl_mtx(mutex_id) == E_OK);
    puts("PASS: validation, ownership, polling, timeout and reacquisition");

    reset_tasks();
    assert(tk_loc_mtx(mutex_id, 0) == E_OK);
    select_task(B);
    schedule_hook = queue_c;
    assert(tk_loc_mtx(mutex_id, TMO_FEVR) == E_OK);
    puts("PASS: FIFO handoff to two waiters and no barging");

    reset_tasks();
    second_id = tk_cre_mtx(&attr);
    assert(second_id == 2);
    assert(tk_loc_mtx(mutex_id, 0) == E_OK);
    assert(tk_loc_mtx(second_id, 0) == E_OK);
    select_task(B);
    schedule_hook = exit_a;
    assert(tk_loc_mtx(mutex_id, TMO_FEVR) == E_OK);
    assert(tk_unl_mtx(mutex_id) == E_OK);
    puts("PASS: task exit releases all owned mutexes and wakes waiter");
    for(INT i = 3; i <= CNF_MAX_MTXID; i++) assert(tk_cre_mtx(&attr) == i);
    assert(tk_cre_mtx(&attr) == E_LIMIT);
    puts("PASS: mutex allocation limit");
    return 0;
}
