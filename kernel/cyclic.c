/* Periodic callbacks run in SysTick context. Management is task-only. */
#include <trykernel.h>
#include <knldef.h>

typedef struct {
    BOOL exists, active;
    T_CCYC config;
    RELTIM remaining;
} CYCCB;
static CYCCB cyclic[CNF_MAX_CYCID];

ID tk_cre_cyc(const T_CCYC *pk_ccyc)
{
    UINT intsts;
    ID i;
    if(is_interrupt_context()) return E_CTX;
    if(pk_ccyc == NULL || pk_ccyc->cychdr == NULL ||
       pk_ccyc->cyctim <= 0 || pk_ccyc->cycphs < 0) return E_PAR;
    if((pk_ccyc->cycatr & ~(TA_HLNG | TA_STA)) != 0) return E_RSATR;
    DI(intsts);
    for(i = 0; i < CNF_MAX_CYCID; i++) {
        if(!cyclic[i].exists) {
            cyclic[i].config = *pk_ccyc;
            cyclic[i].remaining = pk_ccyc->cycphs;
            cyclic[i].active = (pk_ccyc->cycatr & TA_STA) != 0;
            cyclic[i].exists = TRUE;
            EI(intsts);
            return i + 1;
        }
    }
    EI(intsts);
    return E_LIMIT;
}

/* All accesses, including state inspection, are protected from SysTick. */
static ER control(ID id, INT operation, T_RCYC *status)
{
    UINT intsts;
    CYCCB *cb;
    if(is_interrupt_context()) return E_CTX;
    if(id < 1 || id > CNF_MAX_CYCID) return E_ID;
    if(operation == 3 && status == NULL) return E_PAR;
    DI(intsts);
    cb = &cyclic[id - 1];
    if(!cb->exists) {
        EI(intsts);
        return E_NOEXS;
    }
    switch(operation) {
    case 0:
        cb->remaining = cb->config.cycphs;
        cb->active = TRUE;
        break;
    case 1: cb->active = FALSE; break;
    case 2: cb->active = FALSE; cb->exists = FALSE; break;
    case 3:
        status->cycstat = cb->active ? TCYC_STA : TCYC_STP;
        status->lfttim = cb->active ? cb->remaining : 0;
        break;
    }
    EI(intsts);
    return E_OK;
}
ER tk_sta_cyc(ID id) { return control(id, 0, NULL); }
ER tk_stp_cyc(ID id) { return control(id, 1, NULL); }
ER tk_del_cyc(ID id) { return control(id, 2, NULL); }
ER tk_ref_cyc(ID id, T_RCYC *status) { return control(id, 3, status); }

void cyclic_tick(void)
{
    UINT intsts;
    ID i;
    DI(intsts);
    for(i = 0; i < CNF_MAX_CYCID; i++) {
        CYCCB *cb = &cyclic[i];
        if(!cb->exists || !cb->active) continue;
        if(cb->remaining > TIMER_PERIOD) {
            cb->remaining -= TIMER_PERIOD;
            continue;
        }
        /* Keep the nominal phase for periods not divisible by the tick.
         * Coalesce sub-tick expirations into at most one callback per tick.
         * No addition of two potentially large signed times is needed. */
        RELTIM overdue = TIMER_PERIOD - cb->remaining;
        cb->remaining = cb->config.cyctim - overdue % cb->config.cyctim;
        cb->config.cychdr(cb->config.exinf);
    }
    EI(intsts);
}
