/**
 * @file    task_msgtest.h
 * @brief   メッセージキュー試験の初期化・送信・受信APIを宣言する。
 */

#ifndef TASK_MSGTEST_H
#define TASK_MSGTEST_H

#include <trykernel.h>

ER task_msgtest_init(void);
ER task_msgtest_send(UW *sequence);
void task_msgtest_run_tests(void);
void task_msgtest(INT stacd, void *exinf);

#endif /* TASK_MSGTEST_H */
