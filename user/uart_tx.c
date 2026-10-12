/**
 * @file    uart_tx.c
 * @brief   UART送信キューと送信処理を実装する。
 *
 * @details
 * 送信要求をリングキューへ登録し、イベントフラグで
 * UART送信タスクへ通知する。
 * UART送信タスクはキューからメッセージを取り出し、
 * UARTドライバを使用して送信する。
 *
 * @author  Kazu
 * @date    2026-10-03
 */

/*
 * 送信処理の流れ:
 *
 * uart_tx_send()
 *     -> 送信キューへ登録
 *     -> イベントフラグで送信タスクを起床
 *     -> uart_tx_receive()
 *     -> uart_puts()
 *     -> UART FIFOへ送信
 */
#include <trykernel.h>

#include "uart_tx.h"
#include "uart_tx_internal.h"
#include "mini_printf.h"

#define UART_TX_QUEUE_SIZE      32U     //!< 送信キューの最大件数
#define UART_TX_EVENT_DATA  (1U << 0)   //!< データ到着イベント

typedef struct {
    char text[UART_TX_MESSAGE_SIZE];    //!< 送信メッセージ
} UART_TX_MESSAGE;

static UART_TX_MESSAGE uart_tx_queue[UART_TX_QUEUE_SIZE]; //!< 送信キューのリングバッファ

static UW uart_tx_head;     //!< キュー管理変数:次に書き込む位置
static UW uart_tx_tail;     //!< キュー管理変数:次に読み出す位置
static UW uart_tx_count;    //!< キュー管理変数:現在登録されている件数

static ID uart_tx_semid;    //!< キュー保護用セマフォID

static volatile UW uart_tx_overflow_count; //!< 送信キューオーバーフローカウント

static ID uart_tx_flgid; //!< 送信タスク通知用イベントフラグID

/* 内部関数 */
static ER uart_tx_queue_push(const char *text); //!< 送信キューにデータを追加
static ER uart_tx_queue_pop(char *text);        //!< 送信キューからデータを取得
static void uart_tx_copy_string(char *dst, const char *src, UW size); //!< 文字列をコピー

/**
 * @brief UART送信機能を初期化する。
 *
 * キュー管理変数を初期化し、キュー保護用セマフォと
 * 送信タスク通知用イベントフラグを生成する。
 *
 * @retval E_OK 初期化に成功した。
 * @retval E_ID オブジェクトIDを取得できなかった。
 * @retval E_LIMIT カーネルオブジェクト数の上限に達した。
 */
ER uart_tx_init(void)
{
    T_CSEM csem;
    T_CFLG cflg;
    /*
     * セマフォ設定（バイナリセマフォ）
     * 初期カウント:1
     * 最大カウント:1
     */
    csem.sematr  = TA_TFIFO | TA_FIRST;
    csem.isemcnt = 1;
    csem.maxsem  = 1;
    /*
     * イベントフラグ設定
     * 初期イベントバターン:0 (データなし)
     */
    cflg.iflgptn = 0U;
    cflg.flgatr = TA_TFIFO;
    /*
     * リングキュー設定(データなし)
     */
    uart_tx_head  = 0;
    uart_tx_tail  = 0;
    uart_tx_count = 0;
    /* overflowカウント初期化 */
    uart_tx_overflow_count = 0;
    /*
     * キュー保護用セマフォを生成
     */
    uart_tx_semid = tk_cre_sem(&csem);
    if (uart_tx_semid < E_OK) {
        return (ER)uart_tx_semid;
    }

    /*
     * データ到着通知用イベントフラグを生成
     */
    uart_tx_flgid = tk_cre_flg(&cflg);
    if (uart_tx_flgid < E_OK) {
        return (ER)uart_tx_flgid;
    }

    return E_OK;
}
/**
 * @brief  他のタスクが送信要求を登録するためのAPI
 *
 * 1. 送信キューをロックするためにセマフォを取得する。
 * 2. 送信キューに文字列を登録する。
 * 3. 送信キューのロックを解除するためにセマフォを解放する。
 * 4. UART送信タスクへデータ到着を通知するためにイベントフラグをセットする。
 *
 * @param[in] text 送信するNULL終端文字列。
 *
 * @return ER  エラーコード
 * @retval E_OK   送信データキューに登録できた。
 * @retval E_PAR  パラメータに問題があった。
 * @retval E_CTX  割込みコンテキストから呼び出された。
 * @retval E_ID  ID番号が不正だった。
 * @retval E_NOEXS 指定されたセマフォが存在しなかった。
 * @retval E_TMOUT タイムアウトが発生した。
 * @retval E_QOVR キューが満杯だった。
 */
ER uart_tx_send(const char *text)
{
    ER err;
    ER unlock_err;
    // キュー保護用セマフォを取得する。
    // セマフォが解放されるまで無期限に待つ。
    if (text == NULL) {
        return E_PAR;
    }

    // キューをロック:セマフォ取得(無限待ち)
    err = tk_wai_sem(uart_tx_semid, 1, TMO_FEVR);
    if (err < E_OK) {
        return err;
    }
    // 文字列をリングキューに追加する
    err = uart_tx_queue_push(text);

     // pushの成否に関係なくロックを解除する
    unlock_err = tk_sig_sem(uart_tx_semid, 1);

    if (err < E_OK) {
        return err;
    }

    if (unlock_err < E_OK) {
        return unlock_err;
    }

    // UART送信タスクへデータ到着を通知
    return tk_set_flg(
        uart_tx_flgid,
        UART_TX_EVENT_DATA
    );
}
/**
 * @brief UART送信オーバーフローカウントを取得するAPI
 *
 * @return UW  オーバーフローカウント
 */
UW uart_tx_get_overflow_count(void){
    return uart_tx_overflow_count;
}
/**
 * @brief 送信タスクがデータ到着を待つためのAPI
 * 送信タスクはこのAPIを呼び出して、送信要求が登録されるまで待つ(停止状態)
 * TWF_BITCLRを指定しているので、送信要求が登録されると
 * イベントフラグの該当ビットがクリアされる。
 *
 * @param なし
 * @return ER  エラーコード
 * @retval E_OK   送信データ到着イベントを受け取った
 * @retval E_CTX  割込みコンテキストから呼び出された。
 * @retval E_ID  ID番号が不正だった。
 * @retval E_PAR  パラメータに問題があった。
 */
ER uart_tx_wait(void){
    UINT flgptn;
    return tk_wai_flg(uart_tx_flgid,
        UART_TX_EVENT_DATA,     // データ到着ビット
        TWF_ORW | TWF_BITCLR,   // 指定ビットのどれかが立てば解除、ビットクリア
        &flgptn,
        TMO_FEVR);              // イベントが来るまで無限待ち
}
/**
 * @brief 文字列をフォーマットして送信するAPI
 * 簡易的なprintfのような機能を提供する。

 * @param format フォーマット文字列
 * @param ... 可変長引数
 *
 * @return ER  エラーコード
 * @retval E_OK   送信に成功した。
 * @retval <0 mini_vsnprintfのエラーコード
 */
ER uart_tx_printf(const char *format, ...){
    char text[UART_TX_MESSAGE_SIZE];
    va_list args;
    INT length;

    if(format == NULL){
        return E_PAR;
    }
    va_start(args, format);
    length = mini_vsnprintf(text, sizeof(text), format, args);
    va_end(args);
    if(length < 0){
        return (ER)length;
    }
    return uart_tx_send(text);
}
/**
 * @brief UART送信キューからメッセージを1件取り出す。
 *
 * キューをセマフォで保護し、先頭のメッセージを
 * 呼び出し側のバッファへコピーする。
 *
 * @param[out] text 取り出した文字列を格納するバッファ。
 *                  UART_TX_MESSAGE_SIZEバイト以上必要。
 *
 * @retval E_OK    メッセージを取得した。
 * @retval E_PAR   textがNULLだった。
 * @retval E_TMOUT 送信キューが空だった。
 */
ER uart_tx_receive(char *text)
{
    ER err;
    ER unlock_err;
    if(text == NULL){
        return E_PAR;
    }
    /* セマフォ取得 */
    err = tk_wai_sem(uart_tx_semid, 1, TMO_FEVR);
    if (err < E_OK) {
        return err;
    }
    /* キューから一件取得 */
    err = uart_tx_queue_pop(text);
    /* 解放 */
    unlock_err = tk_sig_sem(uart_tx_semid, 1);

    if (err < E_OK) {
        return err;
    }

    return unlock_err;
}
/**
 * @brief 送信キューへメッセージを1件追加する。
 *
 * この関数はキュー保護用セマフォを取得した状態で
 * 呼び出す必要がある。
 *
 * @param[in] text 追加するNULL終端文字列。
 *
 * @retval E_OK   メッセージを追加した。
 * @retval E_QOVR 送信キューが満杯だった。
 */
static ER uart_tx_queue_push(const char *text)
{
    if (uart_tx_count >= UART_TX_QUEUE_SIZE) {
        uart_tx_overflow_count++;
        return E_QOVR;
    }

    uart_tx_copy_string(
        uart_tx_queue[uart_tx_head].text,
        text,
        UART_TX_MESSAGE_SIZE
    );

    uart_tx_head++;

    if (uart_tx_head >= UART_TX_QUEUE_SIZE) {
        uart_tx_head = 0;
    }

    uart_tx_count++;

    return E_OK;
}
/**
 * @brief   Uart送信キューから1件取得する内部関数
 * 送信キューから1件取得する内部関数
 *
 * @param[out] text 取得した文字列を格納するバッファ。
 *                  UART_TX_MESSAGE_SIZEバイト以上必要。
 * @return ER  エラーコード
 * @retval E_OK   送信に成功した。
 * @retval E_TMOUT タイムアウト
 * @retval E_PAR  パラメータに問題があった。
 */
static ER uart_tx_queue_pop(char *text)
{
    if (text == NULL) {
        return E_PAR;
    }

    if (uart_tx_count == 0) { // キューが空
        return E_TMOUT;
    }
    /* 現在のtailから文字列を読み出す */
    uart_tx_copy_string(
        text,
        uart_tx_queue[uart_tx_tail].text,
        UART_TX_MESSAGE_SIZE
    );
    /* tailを進める */
    uart_tx_tail++;
    if (uart_tx_tail >= UART_TX_QUEUE_SIZE) {
        uart_tx_tail = 0;
    }
    /* 登録件数を減らす*/
    uart_tx_count--;

    return E_OK;
}

/**
 * @brief 指定されたサイズ内で文字列をコピーする。
 *
 * size - 1文字までコピーし、コピー先の末尾に
 * 必ずNULL文字を格納する。
 * コピー先またはコピー元がNULLの場合は何もしない。
 *
 * @param[out] dst 文字列のコピー先。
 * @param[in]  src 文字列のコピー元。
 * @param[in]  size コピー先バッファのサイズ。
 */
static void uart_tx_copy_string(char *dst, const char *src, UW size){
    UW i;

    if ((size == 0U) ||
        (dst == NULL) ||
        (src == NULL)) {
        return;
    }

    for (i = 0; i < (size - 1U); i++) {
        if (src[i] == '\0') {
            break;
        }

        dst[i] = src[i];
    }

    dst[i] = '\0';
}
