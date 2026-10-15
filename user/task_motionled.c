/**
 * @file    task_motionled.c
 * @brief   動作状態に応じてモーションLEDを制御する。
 *
 * @details
 * GPIO16のLEDを消灯、点灯、または点滅させる。状態変更はイベントフラグで
 * タスクへ通知し、静止準備中と静止中はそれぞれ異なる周期で点滅させる。
 */

#include <trykernel.h>
#include "gpio.h"
#include "task_motionled.h"
#include "uart_tx.h"

#define MOTION_LED_PIN              16U        //!< モーションLEDを接続するGPIOピン番号。
#define MOTION_LED_EVENT_CHANGED    (1U << 0) //!< LED状態変更を通知するイベントフラグ。
#define MOTION_LED_SETTLING_PERIOD  300        //!< 静止準備中の点滅周期（ミリ秒）。
#define MOTION_LED_STILL_PERIOD     1000       //!< 静止中の点滅周期（ミリ秒）。

/**
 * @brief モーションLEDの動作状態。
 */
typedef enum {
    MOTION_LED_MODE_OFF,      //!< LEDを消灯する。
    MOTION_LED_MODE_SETTLING, //!< 静止準備中を点滅で示す。
    MOTION_LED_MODE_MOVING,   //!< 移動中を点灯で示す。
    MOTION_LED_MODE_STILL     //!< 静止中を点滅で示す。
} MOTION_LED_MODE;

static ID motion_led_flgid;                        //!< 状態変更通知に使用するイベントフラグID。
static volatile MOTION_LED_MODE motion_led_mode;   //!< 現在要求されているLED状態。

/**
 * @brief モーションLEDの状態を設定し、制御タスクへ変更を通知する。
 * @param[in] mode 設定するLED動作状態。
 */
static void motion_led_set_mode(MOTION_LED_MODE mode)
{
    motion_led_mode = mode;
    if(motion_led_flgid > 0){
        (void)tk_set_flg(motion_led_flgid, MOTION_LED_EVENT_CHANGED);
    }
}

/**
 * @brief モーションLEDのGPIOと状態変更イベントフラグを初期化する。
 * @retval E_OK 初期化に成功した。
 * @return イベントフラグ作成に失敗した場合はTRYカーネルのエラーコード。
 */
ER task_motionled_init(void)
{
    T_CFLG cflg;

    gpio_init_out(MOTION_LED_PIN);
    gpio_clear(MOTION_LED_PIN);
    motion_led_mode = MOTION_LED_MODE_OFF;

    cflg.flgatr = TA_TFIFO;
    cflg.iflgptn = 0U;
    motion_led_flgid = tk_cre_flg(&cflg);
    if(motion_led_flgid < E_OK){
        return (ER)motion_led_flgid;
    }
    return E_OK;
}

/**
 * @brief モーションLEDを消灯状態に設定する。
 */
void task_motionled_set_off(void)
{
    motion_led_set_mode(MOTION_LED_MODE_OFF);
}

/**
 * @brief モーションLEDを静止準備中の点滅状態に設定する。
 */
void task_motionled_set_settling(void)
{
    motion_led_set_mode(MOTION_LED_MODE_SETTLING);
}

/**
 * @brief モーションLEDを移動中の点灯状態に設定する。
 */
void task_motionled_set_moving(void)
{
    motion_led_set_mode(MOTION_LED_MODE_MOVING);
}

/**
 * @brief モーションLEDを静止中の点滅状態に設定する。
 */
void task_motionled_set_still(void)
{
    motion_led_set_mode(MOTION_LED_MODE_STILL);
}

/**
 * @brief 要求された動作状態に応じてモーションLEDを制御するタスク。
 * @param[in] stacd タスク起動コード（この関数では使用しない）。
 * @param[in] exinf タスク拡張情報（この関数では使用しない）。
 * @details 状態変更イベントを待ち、移動中は点灯を維持する。
 *          静止準備中と静止中は、それぞれ設定された周期でLEDを反転する。
 */
void task_motionled(INT stacd, void *exinf)
{
    MOTION_LED_MODE applied_mode = MOTION_LED_MODE_OFF;
    UINT flgptn;
    TMO timeout;
    ER err;

    (void)stacd;
    (void)exinf;

    gpio_clear(MOTION_LED_PIN);
    uart_tx_send("Motion LED task start (GPIO16)\r\n");

    while(TRUE){
        if(motion_led_mode != applied_mode){
            applied_mode = motion_led_mode;
            if(applied_mode == MOTION_LED_MODE_OFF){
                gpio_clear(MOTION_LED_PIN);
            }else{
                /* 点滅モードはHighから開始し、移動中は点灯を維持する */
                gpio_set(MOTION_LED_PIN);
            }
        }

        if(applied_mode == MOTION_LED_MODE_SETTLING){
            timeout = MOTION_LED_SETTLING_PERIOD;
        }else if(applied_mode == MOTION_LED_MODE_STILL){
            timeout = MOTION_LED_STILL_PERIOD;
        }else{
            timeout = TMO_FEVR;
        }

        err = tk_wai_flg(
            motion_led_flgid,
            MOTION_LED_EVENT_CHANGED,
            TWF_ORW | TWF_BITCLR,
            &flgptn,
            timeout
        );

        if((err == E_TMOUT)
                && ((applied_mode == MOTION_LED_MODE_SETTLING)
                    || (applied_mode == MOTION_LED_MODE_STILL))){
            gpio_toggle(MOTION_LED_PIN);
        }
    }
}
