#include <trykernel.h>
#include "gpio.h"
#include "motor.h"
#include "i2c.h"
#include "uart_sync.h"
#include "uart_tx.h"
#include "task_uartrx.h"
#include "task_distance.h"

extern void task_uarttx(INT stacd, void *exinf);
extern void task_led1(INT stacd, void *exinf);

static UW stack_tx[1024 / sizeof(UW)];
static UW stack_rx[1024 / sizeof(UW)];
static UW stack_led[1024 / sizeof(UW)];
static UW stack_distance[1024 / sizeof(UW)];

static ER start_task(FP entry, PRI priority, UW *stack, SZ size)
{
    T_CTSK config = {
        .tskatr = TA_HLNG | TA_RNG3 | TA_USERBUF,
        .task = entry,
        .itskpri = priority,
        .stksz = size,
        .bufptr = stack,
    };
    ID id = tk_cre_tsk(&config);
    if(id < E_OK) return (ER)id;
    return tk_sta_tsk(id, 0);
}

/* 2WDの初期構成。取り外したセンサーやFlashのタスクは起動しない。 */
int usermain(void)
{
    ER err;
    motor_init();
    led25_init();
    i2c0_init();
    err = i2c0_sync_init();
    if(err < E_OK) return err;
    err = uart_sync_init();
    if(err < E_OK) return err;
    err = uart_tx_init();
    if(err < E_OK) return err;
    err = task_uartrx_init();
    if(err < E_OK) return err;
    err = start_task((FP)task_uarttx, 6, stack_tx, sizeof(stack_tx));
    if(err < E_OK) return err;
    uart_tx_send("TryKernel 2WD: UART / LED / motor ready\r\n");
    err = start_task((FP)task_led1, 12, stack_led, sizeof(stack_led));
    if(err < E_OK) return err;
    err = start_task((FP)task_distance, 10,
                     stack_distance, sizeof(stack_distance));
    if(err < E_OK) return err;
    return start_task((FP)task_uartrx, 4, stack_rx, sizeof(stack_rx));
}
