/**
 * @file    task_motionled.h
 * @brief   モーションLEDタスクの初期化・制御APIを宣言する。
 *
 * @details モーションLEDタスクの起動関数と、消灯・静止準備・移動・静止の
 *          各状態を要求するAPIを公開する。
 */

#ifndef TASK_MOTIONLED_H
#define TASK_MOTIONLED_H

#include <trykernel.h>

ER task_motionled_init(void);
void task_motionled(INT stacd, void *exinf);
void task_motionled_set_off(void);
void task_motionled_set_settling(void);
void task_motionled_set_moving(void);
void task_motionled_set_still(void);

#endif /* TASK_MOTIONLED_H */
