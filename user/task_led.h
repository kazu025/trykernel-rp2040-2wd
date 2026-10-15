/**
 * @file    task_led.h
 * @brief   LED制御タスクと状態設定APIを宣言する。
 *
 * @details LED制御タスクの起動関数と、点灯・消灯・点滅の状態設定および
 *          現在状態の文字列取得に使用するAPIを公開する。
 */

#ifndef TASK_LED_H
#define TASK_LED_H

#include <trykernel.h>

void task_led1(INT stacd, void *exinf);

void led_task_set_on(void);
void led_task_set_off(void);
void led_task_blink(void);
const char *led_task_mode_name(void);

#endif /* TASK_LED_H */
