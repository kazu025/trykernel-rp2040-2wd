/**
 * @file    task_mutexpi.h
 * @brief   ミューテックス優先度継承試験の初期化・実行APIを宣言する。
 */

#ifndef TASK_MUTEXPI_H
#define TASK_MUTEXPI_H

#include <trykernel.h>

ER task_mutexpi_init(void);
void task_mutexpi_run_test(void);

#endif /* TASK_MUTEXPI_H */
