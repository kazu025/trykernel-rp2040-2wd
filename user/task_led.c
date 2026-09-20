#include <trykernel.h>
#include "gpio.h"
#include "task_led.h"
#include "uart_tx.h"

typedef enum {
    LED_MODE_OFF,
    LED_MODE_ON,
    LED_MODE_BLINK
} LED_TASK_MODE;
static volatile LED_TASK_MODE led_task_mode = LED_MODE_OFF;
void led_task_set_on(void) {
    led_task_mode = LED_MODE_ON;
}
void led_task_set_off(void) {
    led_task_mode = LED_MODE_OFF;
}
void led_task_blink(void) {
    led_task_mode = LED_MODE_BLINK;
}
const char *led_task_mode_name(void) {
    switch (led_task_mode) {
        case LED_MODE_OFF:
            return "OFF";
        case LED_MODE_ON:
            return "ON";
        case LED_MODE_BLINK:
            return "BLINK";
        default:
            return "UNKNOWN";
    }
}
/* 100msごとの通知を数える。タスク再起動時にも再利用する。 */
static ID led_tick_semid;

/* SysTick内で実行するので、通知だけを行う。 */
static void led_periodic_handler(void *exinf)
{
    ID semid = *(ID *)exinf;
    /* 長時間タスクが動けない場合は最大10回分まで保持する。 */
    (void)tk_sig_sem(semid, 1);
}

/* LED制御タスク1の実行関数 */
void task_led1(INT stacd, void *exinf)
{
    (void)stacd;
    (void)exinf;
    T_CCYC ccyc = {
        .exinf = &led_tick_semid,
        .cycatr = TA_HLNG,       // 準備完了後に開始する
        .cychdr = led_periodic_handler,
        .cyctim = 100,
        .cycphs = 100,
    };
    T_CSEM csem = { .sematr = TA_TFIFO, .isemcnt = 0, .maxsem = 10 };
    ID cycid;
    ER err;
    INT ticks;

    cycid = tk_cre_cyc(&ccyc);
    if(cycid < E_OK) {
        uart_tx_send("LED: cyclic creation failed\r\n");
        tk_ext_tsk();
        return;
    }
    if(led_tick_semid <= 0) led_tick_semid = tk_cre_sem(&csem);
    if(led_tick_semid < E_OK) {
        tk_del_cyc(cycid);
        uart_tx_send("LED: semaphore creation failed\r\n");
        tk_ext_tsk();
        return;
    }
    /* 再起動前の通知が残っていれば取り除く。 */
    while(tk_wai_sem(led_tick_semid, 1, TMO_POL) == E_OK) {}
    uart_tx_send("LED task start!!\r\n");
    err = tk_sta_cyc(cycid);
    if(err != E_OK) {
        tk_del_cyc(cycid);
        uart_tx_send("LED: cyclic start failed\r\n");
        tk_ext_tsk();
        return;
    }
    while(1) {
        switch (led_task_mode) {
            case LED_MODE_OFF:
                led25_off();
                ticks = 1;                     // 100ms通知を1回待つ
                break;
            case LED_MODE_ON:
                led25_on();
                ticks = 1;                     // 100ms通知を1回待つ
                break;
            case LED_MODE_BLINK:
            default:
                led25_toggle();
                ticks = 10;                    // 100ms通知を10回待つ
                break;          
        }
        err = tk_wai_sem(led_tick_semid, ticks, TMO_FEVR);
        if(err != E_OK) {
            tk_del_cyc(cycid);
            uart_tx_send("LED: periodic wait failed\r\n");
            tk_ext_tsk();
            return;
        }
    }
}
