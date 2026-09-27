#include <trykernel.h>
#include "motor.h"
#include "task_motor.h"
#include "uart_tx.h"

static ID motor_tick_semid;
static void motor_periodic_handler(void *exinf)
{
    (void)exinf;
    /* 滞留分は1回にまとめ、急な追い付き更新を避ける。 */
    (void)tk_sig_sem(motor_tick_semid, 1);
}

ER task_motor_init(void)
{
    static const T_CSEM csem = { .sematr = TA_TFIFO, .isemcnt = 0, .maxsem = 1 };
    static const T_CCYC ccyc = {
        .exinf = NULL,
        .cycatr = TA_HLNG, .cychdr = motor_periodic_handler,
        .cyctim = 50, .cycphs = 50,
    };
    ID cycid;
    ER err;
    motor_tick_semid = tk_cre_sem(&csem);
    if(motor_tick_semid < E_OK) return (ER)motor_tick_semid;
    cycid = tk_cre_cyc(&ccyc);
    if(cycid < E_OK){
        return (ER)cycid;
    }
    err = tk_sta_cyc(cycid);
    if(err < E_OK){
        (void)tk_del_cyc(cycid);
    }
    return err;
}

void task_motor(INT stacd, void *exinf)
{
    (void)stacd;
    (void)exinf;
    while(1){
        if(tk_wai_sem(motor_tick_semid, 1, TMO_FEVR) == E_OK) motor_ramp_step();
    }
}
