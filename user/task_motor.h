/**
 * @file    task_motor.h
 * @brief   モーター制御タスクの初期化関数と実行関数を宣言する。
 */

#ifndef TASK_MOTOR_H
#define TASK_MOTOR_H
#include <trykernel.h>
ER task_motor_init(void);
void task_motor(INT stacd, void *exinf);
#endif
