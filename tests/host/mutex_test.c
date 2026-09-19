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
static ID inherit_id;
static ID chain_id;
#define A (&tcb_tbl[0])
#define B (&tcb_tbl[1])
#define C (&tcb_tbl[2])
#define D (&tcb_tbl[3])

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
        tcb_tbl[i].btskpri = i + 1;
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

static void release_inherited_mutex(void)
{
    assert(A->state == TS_WAIT);
    assert(C->btskpri == 3 && C->itskpri == 1);
    assert(ready_queue[0] == C);
    assert(ready_queue[1] == B);
    select_task(C);
    assert(tk_unl_mtx(inherit_id) == E_OK);
    assert(C->itskpri == 3);
    select_task(A);
}

static void priority_inheritance_timeout(void)
{
    assert(C->itskpri == 1);
    systimer_handler();
    assert(C->itskpri == 1);
    systimer_handler();
    assert(A->state == TS_READY);
    assert(C->itskpri == 3);
    select_task(A);
}

static void release_multiple_mutexes(void)
{
    assert(C->itskpri == 1);
    select_task(C);
    assert(tk_unl_mtx(inherit_id) == E_OK);
    assert(C->itskpri == 2); /* B still waits for chain_id. */
    assert(tk_unl_mtx(chain_id) == E_OK);
    assert(C->itskpri == 3);
    select_task(A);
}

static void queue_high_for_multiple_mutexes(void)
{
    assert(C->itskpri == 2);
    select_task(A);
    schedule_hook = release_multiple_mutexes;
    assert(tk_loc_mtx(inherit_id, TMO_FEVR) == E_OK);
    assert(tk_unl_mtx(inherit_id) == E_OK);
    select_task(B);
}

static void release_chain_owner(void)
{
    assert(B->itskpri == 1);
    assert(C->itskpri == 1); /* A -> B -> C dynamic inheritance. */
    select_task(C);
    assert(tk_unl_mtx(chain_id) == E_OK);
    assert(C->itskpri == 3);
    select_task(B);
    assert(tk_unl_mtx(inherit_id) == E_OK);
    assert(B->itskpri == 2);
    select_task(A);
}

static void queue_high_for_chain(void)
{
    assert(C->itskpri == 2);
    select_task(A);
    schedule_hook = release_chain_owner;
    assert(tk_loc_mtx(inherit_id, TMO_FEVR) == E_OK);
    assert(tk_unl_mtx(inherit_id) == E_OK);
    select_task(B);
}

static void release_to_highest_waiter(void)
{
    assert(wait_queue == B && B->next == A);
    assert(C->itskpri == 1);
    select_task(C);
    assert(tk_unl_mtx(inherit_id) == E_OK);
    assert(A->state == TS_READY && B->state == TS_WAIT);
    select_task(A);
}

static void queue_high_after_low(void)
{
    select_task(A);
    schedule_hook = release_to_highest_waiter;
    assert(tk_loc_mtx(inherit_id, TMO_FEVR) == E_OK);
    assert(tk_unl_mtx(inherit_id) == E_OK);
    select_task(B);
}

static void exit_inherited_owner(void)
{
    assert(C->itskpri == A->itskpri);
    select_task(C);
    tk_ext_tsk();
}

static void exit_inherited_owner_with_ready_peer(void)
{
    reset_tasks();
    D->btskpri = C->btskpri;
    D->itskpri = C->btskpri;
    D->state = TS_READY;
    tqueue_add_entry(&ready_queue[PRI_INDEX(D->itskpri)], D);

    select_task(C);
    assert(tk_loc_mtx(inherit_id, TMO_POL) == E_OK);
    select_task(A);
    schedule_hook = exit_inherited_owner;
    assert(tk_loc_mtx(inherit_id, TMO_FEVR) == E_OK);
    assert(C->state == TS_DORMANT);
    assert(D->state == TS_READY);
    assert(ready_queue[PRI_INDEX(D->itskpri)] == D);
    assert(D->next == NULL);
    assert(A->state == TS_READY);
    assert(A->itskpri == A->btskpri);
    select_task(A);
    assert(tk_unl_mtx(inherit_id) == E_OK);
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

    T_CMTX inherit = {TA_INHERIT};
    inherit_id = tk_cre_mtx(&inherit);
    chain_id = tk_cre_mtx(&inherit);
    assert(inherit_id == 3 && chain_id == 4);

    reset_tasks();
    select_task(C);
    assert(tk_loc_mtx(inherit_id, TMO_POL) == E_OK);
    select_task(A);
    schedule_hook = release_inherited_mutex;
    assert(tk_loc_mtx(inherit_id, TMO_FEVR) == E_OK);
    assert(tk_unl_mtx(inherit_id) == E_OK);
    puts("PASS: basic priority inheritance and restoration");

    reset_tasks();
    select_task(C);
    assert(tk_loc_mtx(inherit_id, TMO_POL) == E_OK);
    select_task(A);
    schedule_hook = priority_inheritance_timeout;
    assert(tk_loc_mtx(inherit_id, TIMER_PERIOD) == E_TMOUT);
    select_task(C);
    assert(tk_unl_mtx(inherit_id) == E_OK);
    puts("PASS: priority restoration after waiter timeout");

    reset_tasks();
    select_task(C);
    assert(tk_loc_mtx(inherit_id, TMO_POL) == E_OK);
    assert(tk_loc_mtx(chain_id, TMO_POL) == E_OK);
    select_task(B);
    schedule_hook = queue_high_for_multiple_mutexes;
    assert(tk_loc_mtx(chain_id, TMO_FEVR) == E_OK);
    assert(tk_unl_mtx(chain_id) == E_OK);
    puts("PASS: strict priority restoration with multiple owned mutexes");

    reset_tasks();
    select_task(C);
    assert(tk_loc_mtx(chain_id, TMO_POL) == E_OK);
    select_task(B);
    assert(tk_loc_mtx(inherit_id, TMO_POL) == E_OK);
    schedule_hook = queue_high_for_chain;
    assert(tk_loc_mtx(chain_id, TMO_FEVR) == E_OK);
    assert(tk_unl_mtx(chain_id) == E_OK);
    puts("PASS: chained dynamic priority inheritance");

    reset_tasks();
    select_task(C);
    assert(tk_loc_mtx(inherit_id, TMO_POL) == E_OK);
    select_task(B);
    schedule_hook = queue_high_after_low;
    assert(tk_loc_mtx(inherit_id, TMO_FEVR) == E_OK);
    assert(tk_unl_mtx(inherit_id) == E_OK);
    puts("PASS: TA_INHERIT selects the highest-priority waiter");

    exit_inherited_owner_with_ready_peer();
    puts("PASS: inherited owner exit removes the exiting task by identity");

    for(INT i = 5; i <= CNF_MAX_MTXID; i++) assert(tk_cre_mtx(&attr) == i);
    assert(tk_cre_mtx(&attr) == E_LIMIT);
    puts("PASS: mutex allocation limit");
    return 0;
}
