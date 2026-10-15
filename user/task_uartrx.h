/**
 * @file    task_uartrx.h
 * @brief   UART受信タスクの初期化と実行インターフェースを宣言する。
 *
 * @details UART受信割り込み通知に使うオブジェクトの初期化関数と、
 *          受信データをコンソールへ渡すタスク関数を公開する。
 */

#ifndef TASK_UARTRX_H
#define TASK_UARTRX_H
/* -------------------------------------------------- */
#include <trykernel.h>
ER task_uartrx_init(void);
void task_uartrx(INT stacd, void *exinf);
/* -------------------------------------------------- */
#endif /* TASK_UARTRX_H */
