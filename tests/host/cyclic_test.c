#include <trykernel.h>
#include <knldef.h>
#include <assert.h>
#include <stdio.h>
#include <limits.h>
UINT host_primask;
BOOL host_interrupt;
TCB *ready_queue[CNF_MAX_TSKPRI];
static INT count, schedules;
static ID current;
void scheduler(void) { schedules++; }
void mutex_wait_timeout(TCB *tcb) { (void)tcb; }
static void callback(void *arg)
{
    assert(arg == &count);
    assert(host_interrupt && host_primask == 1);
    count++;
    assert(tk_stp_cyc(current) == E_CTX);
}
static void tick(void)
{
    host_interrupt = TRUE;
    systimer_handler();
    host_interrupt = FALSE;
    assert(host_primask == 0);
}
int main(void)
{
    T_CCYC c = { &count, TA_HLNG, callback, 25, 20 };
    T_RCYC r;
    assert(tk_cre_cyc(NULL) == E_PAR);
    c.cyctim = 0; assert(tk_cre_cyc(&c) == E_PAR);
    c.cyctim = -1; assert(tk_cre_cyc(&c) == E_PAR);
    c.cyctim = 25; c.cycphs = -1; assert(tk_cre_cyc(&c) == E_PAR);
    c.cycphs = 20; c.cycatr = 0x8000; assert(tk_cre_cyc(&c) == E_RSATR);
    c.cycatr = TA_HLNG;
    current = tk_cre_cyc(&c); assert(current == 1);
    assert(tk_ref_cyc(current, &r) == E_OK && r.cycstat == TCYC_STP);
    tick(); assert(count == 0);
    assert(tk_sta_cyc(current) == E_OK);
    tick(); assert(count == 0);
    tick(); assert(count == 1);
    tick(); tick(); assert(count == 1);
    tick(); assert(count == 2); /* nominal 45ms, delivered at 50ms */
    tick(); tick(); assert(count == 3); /* nominal 70ms */
    assert(tk_stp_cyc(current) == E_OK);
    tick(); tick(); assert(count == 3);
    assert(tk_ref_cyc(current, &r) == E_OK && r.lfttim == 0);
    assert(tk_sta_cyc(current) == E_OK);
    tick(); assert(count == 3);
    assert(tk_sta_cyc(current) == E_OK); /* restart active handler */
    tick(); assert(count == 3); tick(); assert(count == 4);
    assert(tk_ref_cyc(0, &r) == E_ID);
    assert(tk_sta_cyc(CNF_MAX_CYCID + 1) == E_ID);
    assert(tk_ref_cyc(current, NULL) == E_PAR);
    assert(tk_del_cyc(current) == E_OK);
    assert(tk_sta_cyc(current) == E_NOEXS);
    assert(tk_stp_cyc(current) == E_NOEXS);
    assert(tk_del_cyc(current) == E_NOEXS);
    c.cycatr |= TA_STA; c.cycphs = 0; c.cyctim = 1;
    for(INT i = 0; i < CNF_MAX_CYCID; i++) assert(tk_cre_cyc(&c) == i + 1);
    assert(tk_cre_cyc(&c) == E_LIMIT);
    count = 0; tick(); assert(count == CNF_MAX_CYCID);
    tick(); assert(count == 2 * CNF_MAX_CYCID);
    for(INT i = 1; i <= CNF_MAX_CYCID; i++) assert(tk_del_cyc(i) == E_OK);
    tick(); assert(count == 2 * CNF_MAX_CYCID);
    c.cycphs = INT32_MAX; c.cyctim = INT32_MAX;
    current = tk_cre_cyc(&c); tick();
    assert(tk_ref_cyc(current, &r) == E_OK && r.lfttim == INT32_MAX - 10);
    host_primask = 1; assert(tk_stp_cyc(current) == E_OK); assert(host_primask == 1);
    host_primask = 0; host_interrupt = TRUE;
    assert(tk_cre_cyc(&c) == E_CTX);
    assert(tk_ref_cyc(current, &r) == E_CTX);
    assert(tk_del_cyc(current) == E_CTX);
    assert(tk_sta_cyc(current) == E_CTX);
    assert(schedules > 0);
    puts("cyclic tests passed");
    return 0;
}
