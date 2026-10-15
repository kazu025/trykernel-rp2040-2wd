/**
 * @file    task_inapower.h
 * @brief   INA226測定タスクのデータ型とAPIを宣言する。
 *
 * @details
 * INA226から取得した電圧・電流・電力の測定値を表す構造体と、
 * 測定タスクの初期化、測定値取得、タスク本体のインターフェースを定義する。
 */

#ifndef TASK_INAPOWER_H
#define TASK_INAPOWER_H

#include <trykernel.h>

/**
 * @brief INA226の測定値をまとめて保持する構造体。
 */
typedef struct {
    UW sample_count;     //!< 成功した測定の累積回数。
    UW bus_voltage_mv;   //!< バス電圧（mV）。
    INT current_ua;      //!< 電流（µA）。
    INT power_mw;        //!< 電力（mW）。
} INA226_MEASUREMENT;

void task_inapower_init(void);
BOOL task_inapower_get(INA226_MEASUREMENT *measurement, UW *error_count);
void task_inapower(INT stacd, void *exinf);

#endif /* TASK_INAPOWER_H */
