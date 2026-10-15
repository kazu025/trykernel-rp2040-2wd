/**
 * @file    task_led.c
 * @brief   LEDの点灯状態を切り替えるタスクと制御APIを実装する。
 *
 * @details
 * LEDの消灯、点灯、点滅の状態を保持し、周期通知を使ってLEDを制御する。
 * 状態の変更は制御APIから行い、タスクが状態に応じた点灯処理を継続する。
 */

#include <trykernel.h>
#include "gpio.h"
#include "task_led.h"
#include "uart_tx.h"

/**
 * @brief LED制御タスクが扱う点灯状態。
 */
typedef enum {
    LED_MODE_OFF,   //!< LEDを消灯する。
    LED_MODE_ON,    //!< LEDを点灯する。
    LED_MODE_BLINK  //!< LEDを周期的に点滅させる。
} LED_TASK_MODE;

/** 現在のLED点灯状態。 */
static volatile LED_TASK_MODE led_task_mode = LED_MODE_OFF;

/**
 * @brief LEDの点灯状態を点灯に設定する。
 */
void led_task_set_on(void) {
    led_task_mode = LED_MODE_ON;
}

/**
 * @brief LEDの点灯状態を消灯に設定する。
 */
void led_task_set_off(void) {
    led_task_mode = LED_MODE_OFF;
}

/**
 * @brief LEDの点灯状態を点滅に設定する。
 */
void led_task_blink(void) {
    led_task_mode = LED_MODE_BLINK;
}

/**
 * @brief 現在のLED点灯状態を文字列で取得する。
 * @return 状態名（"OFF"、"ON"、"BLINK"、または"UNKNOWN"）。
 */
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
/** 100 ms周期通知を受け取るセマフォID。 */
static ID led_tick_semid;

/**
 * @brief 周期ハンドラからLED制御タスクへ通知する。
 * @param[in] exinf セマフォIDを格納した領域へのポインタ。
 * @details 周期ハンドラ内ではセマフォ通知のみを行う。
 */
static void led_periodic_handler(void *exinf)
{
    ID semid = *(ID *)exinf;
    /* 長時間タスクが動けない場合は最大10回分まで保持する。 */
    (void)tk_sig_sem(semid, 1);
}

/**
 * @brief 周期通知に従ってLEDを点灯、消灯、または点滅させるタスク。
 * @param[in] stacd タスク起動コード（この関数では使用しない）。
 * @param[in] exinf タスク拡張情報（この関数では使用しない）。
 * @details 周期ハンドラとセマフォを作成して通知を待ち、現在のLED状態に
 *          応じた出力を行う。初期化または待ち処理に失敗した場合はエラーを
 *          UARTへ通知し、タスクを終了する。
 */
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
