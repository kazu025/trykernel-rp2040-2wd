/**
 * @file    inittsk.c
 * @brief   TryKernel起動時の初期タスクを生成・実行する。
 *
 * @details
 * カーネル起動後に最初に実行する初期タスクを定義する。
 *
 * main()では、初期タスク用のTCBとスタックを使用して
 * tk_cre_tsk()により初期タスクを生成し、
 * tk_sta_tsk()により実行を開始する。
 *
 * 初期タスクinitsk()ではUARTを初期化して起動メッセージを出力し、
 * usermain()を呼び出してユーザアプリケーションの初期化と
 * タスク生成を開始する。
 *
 * usermain()から戻った場合は、初期タスク自身をtk_ext_tsk()で終了する。
 *
 * @note
 * 初期タスクの優先度は1であり、初期タスク用スタックは
 * tskstk_ini[]として静的に確保される。
 *
 * @note
 * main()の無限ループは、初期タスクへ処理を移行した後には
 * 通常実行されない。
 */

#include <trykernel.h>
#include <knldef.h>
#include "uart.h"

/**
 * @brief 初期タスクの実行関数。
 *
 * カーネル起動後にUARTを初期化し、
 * usermain()を呼び出してユーザタスクを起動する。
 */
void initsk(INT stacd, void *exinf);

/**
 * @brief 初期タスク用のスタック領域。
 *
 * 256バイト分のスタック領域をUW単位で確保する。
 */
UW tskstk_ini[256 / sizeof(UW)];

/**
 * @brief 初期タスクのID。
 */
ID tskid_ini;

/**
 * @brief 初期タスクの生成情報。
 *
 * 初期タスクの属性、実行関数、優先度、
 * スタックサイズおよびスタック領域を指定する。
 */
T_CTSK ctsk_ini = {
    .tskatr  = TA_HLNG | TA_RNG0 | TA_USERBUF, /**< タスク属性。 */
    .task    = initsk,                          /**< タスクの実行関数。 */
    .itskpri = 1,                               /**< タスクの初期優先度。 */
    .stksz   = sizeof(tskstk_ini),              /**< スタックサイズ。 */
    .bufptr  = tskstk_ini                       /**< スタック領域へのポインタ。 */
};
/**
 * @brief TryKernelの初期タスクを実行する。
 *
 * @details
 * UARTを初期化してカーネル起動メッセージを出力した後、
 * usermain()を呼び出してユーザアプリケーションの初期化を行う。
 *
 * usermain()から戻った場合は、tk_ext_tsk()を呼び出して
 * 初期タスク自身を終了する。
 *
 * @param[in] stacd タスク起動コード。現在は使用しない。
 * @param[in] exinf タスク起動時に渡される拡張情報。現在は使用しない。
 *
 * @note
 * この関数は初期タスクとして実行され、通常はtk_ext_tsk()の後に
 * 呼び出し元へ戻らない。
 */
void initsk(INT stacd, void *exinf)
{
    (void)stacd;
    (void)exinf;
    uart_init();                                  // シリアル通信の初期化
    uart_puts("Start Try Kernel\r\n");

    usermain();
    tk_ext_tsk();
}
/**
 * @brief TryKernelのメインエントリポイント。
 *
 * @details
 * 初期タスクの生成情報を使用してtk_cre_tsk()を呼び出し、
 * 初期タスクを生成する。
 *
 * 生成された初期タスクIDを用いてtk_sta_tsk()を呼び出し、
 * 初期タスクの実行を開始する。
 *
 * 初期タスクへ処理を移行した後、main()自身は通常実行されないため、
 * 念のため無限ループで待機する。
 *
 * @return int 通常は復帰しない。
 */
int main(void)
{
    tskid_ini = tk_cre_tsk(&ctsk_ini);              // 初期タスク生成
    tk_sta_tsk(tskid_ini, 0);                       // 初期タスク実行

     while(1);      // ここは実行されない
}
