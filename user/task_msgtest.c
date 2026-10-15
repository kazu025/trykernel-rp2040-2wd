/**
 * @file    task_msgtest.c
 * @brief   TRYカーネルのメッセージキュー送受信と動作を確認する。
 *
 * @details
 * 通常の送受信用キューとテスト専用キューを作成する。メッセージ送信APIと
 * 受信タスクを提供し、テスト関数ではFIFO順序、キュー満杯、送受信タイムアウトを
 * 確認して結果をUARTへ表示する。
 */

#include <trykernel.h>
#include "task_msgtest.h"
#include "uart_tx.h"

#define MSGTEST_QUEUE_DEPTH  4   //!< メッセージキューの最大格納件数。
#define MSGTEST_TIMEOUT      100 //!< タイムアウト試験で待つ時間（ミリ秒）。

/**
 * @brief メッセージキューで送受信する試験用メッセージ。
 */
typedef struct {
    UW  sequence; //!< メッセージの通番。
    INT value;    //!< 通番に対応する試験値。
} MSGTEST_MESSAGE;

static MSGTEST_MESSAGE msgtest_buffer[MSGTEST_QUEUE_DEPTH];      //!< 通常キューのメッセージ格納バッファ。
static MSGTEST_MESSAGE msgtest_test_buffer[MSGTEST_QUEUE_DEPTH]; //!< キュー動作試験用のメッセージ格納バッファ。
static ID msgtest_queue_id;                                      //!< 通常メッセージキューのID。
static ID msgtest_test_queue_id;                                 //!< 試験用メッセージキューのID。
static UW msgtest_sequence;                                      //!< 通常キューへ送る次の通番。

/**
 * @brief 通常用と試験用のメッセージキューを作成する。
 * @retval E_OK 両方のキュー作成に成功した。
 * @return キュー作成に失敗した場合はTRYカーネルのエラーコード。
 */
ER task_msgtest_init(void)
{
    T_CMSGQ cmsgq;

    cmsgq.msgqatr = TA_TFIFO;
    cmsgq.msgsz = sizeof(MSGTEST_MESSAGE);
    cmsgq.maxmsg = MSGTEST_QUEUE_DEPTH;
    cmsgq.bufptr = msgtest_buffer;

    msgtest_queue_id = tk_cre_msgq(&cmsgq);
    if(msgtest_queue_id < E_OK) {
        return (ER)msgtest_queue_id;
    }

    cmsgq.bufptr = msgtest_test_buffer;
    msgtest_test_queue_id = tk_cre_msgq(&cmsgq);
    if(msgtest_test_queue_id < E_OK) {
        return (ER)msgtest_test_queue_id;
    }
    return E_OK;
}

/**
 * @brief 通常メッセージキューへ通番付きメッセージを送信する。
 * @param[out] sequence 送信に成功したメッセージの通番を格納する領域。
 * @retval E_OK メッセージをキューへ送信した。
 * @retval E_PAR sequenceがNULLである。
 * @retval E_NOEXS 通常メッセージキューが存在しない。
 * @return その他、キュー送信時に発生したTRYカーネルのエラーコード。
 */
ER task_msgtest_send(UW *sequence)
{
    MSGTEST_MESSAGE message;
    ER err;

    if(sequence == NULL) return E_PAR;
    if(msgtest_queue_id <= 0) return E_NOEXS;

    message.sequence = ++msgtest_sequence;
    message.value = (INT)(message.sequence * 10U);
    err = tk_snd_msgq(msgtest_queue_id, &message, TMO_POL);
    if(err == E_OK) {
        *sequence = message.sequence;
    }
    return err;
}

/**
 * @brief メッセージキューのFIFO、満杯、送受信タイムアウトを試験する。
 * @details 各試験結果をUARTへ表示し、最後に成功数を出力する。
 *          試験途中でセットアップ、受信、後片付けに失敗した場合はその時点で終了する。
 */
void task_msgtest_run_tests(void)
{
    MSGTEST_MESSAGE message;
    ER err;
    INT i;
    INT passed = 0;

    if(msgtest_test_queue_id <= 0) {
        uart_tx_send("Message queue test is not initialized\r\n");
        return;
    }

    uart_tx_send("Message queue test start\r\n");

    /* 深さ4のキューへ、順序が分かる4件のメッセージを格納する */
    for(i = 0; i < MSGTEST_QUEUE_DEPTH; i++) {
        message.sequence = (UW)(i + 1);
        message.value = (i + 1) * 10;
        err = tk_snd_msgq(msgtest_test_queue_id, &message, TMO_POL);
        if(err != E_OK) {
            uart_tx_printf("FIFO setup error: index=%d error=%d\r\n", i, err);
            return;
        }
    }

    /* 5件目は、待たずに送信するとキュー満杯でE_TMOUTになる */
    message.sequence = 5U;
    message.value = 50;
    err = tk_snd_msgq(msgtest_test_queue_id, &message, TMO_POL);
    if(err == E_TMOUT) {
        uart_tx_send("Queue full test: PASS (E_TMOUT)\r\n");
        passed++;
    } else {
        uart_tx_printf("Queue full test: FAIL (error=%d)\r\n", err);
    }

    /* 格納した4件が送信順に取り出されることを確認する */
    for(i = 0; i < MSGTEST_QUEUE_DEPTH; i++) {
        err = tk_rcv_msgq(msgtest_test_queue_id, &message, TMO_POL);
        if((err == E_OK) &&
           (message.sequence == (UW)(i + 1)) &&
           (message.value == ((i + 1) * 10))) {
            uart_tx_printf("FIFO receive: sequence=%u value=%d\r\n",
                           (UINT)message.sequence, message.value);
        } else {
            uart_tx_printf("FIFO test: FAIL (index=%d error=%d)\r\n", i, err);
            return;
        }
    }
    uart_tx_send("FIFO order test: PASS\r\n");
    passed++;

    /* 空のキューで最大100ms待ち、受信タイムアウトを確認する */
    err = tk_rcv_msgq(msgtest_test_queue_id, &message, MSGTEST_TIMEOUT);
    if(err == E_TMOUT) {
        uart_tx_send("Receive timeout test: PASS (100ms, E_TMOUT)\r\n");
        passed++;
    } else {
        uart_tx_printf("Receive timeout test: FAIL (error=%d)\r\n", err);
    }

    /* 再び満杯にして最大100ms待ち、送信タイムアウトを確認する */
    for(i = 0; i < MSGTEST_QUEUE_DEPTH; i++) {
        message.sequence = (UW)(i + 11);
        message.value = (i + 11) * 10;
        err = tk_snd_msgq(msgtest_test_queue_id, &message, TMO_POL);
        if(err != E_OK) {
            uart_tx_printf("Send timeout setup error: index=%d error=%d\r\n", i, err);
            return;
        }
    }

    message.sequence = 15U;
    message.value = 150;
    err = tk_snd_msgq(msgtest_test_queue_id, &message, MSGTEST_TIMEOUT);
    if(err == E_TMOUT) {
        uart_tx_send("Send timeout test: PASS (100ms, E_TMOUT)\r\n");
        passed++;
    } else {
        uart_tx_printf("Send timeout test: FAIL (error=%d)\r\n", err);
    }

    /* 次回も同じテストを実行できるよう、残った4件を取り出す */
    for(i = 0; i < MSGTEST_QUEUE_DEPTH; i++) {
        err = tk_rcv_msgq(msgtest_test_queue_id, &message, TMO_POL);
        if(err != E_OK) {
            uart_tx_printf("Message queue cleanup error: %d\r\n", err);
            return;
        }
    }

    uart_tx_printf("Message queue test complete: %d/4 PASS\r\n", passed);
}

/**
 * @brief 通常メッセージキューからメッセージを受信してUARTへ表示するタスク。
 * @param[in] stacd タスク起動コード（この関数では使用しない）。
 * @param[in] exinf タスク拡張情報（この関数では使用しない）。
 * @details キューから無期限待ちで受信し、受信データまたはエラーを表示する。
 */
void task_msgtest(INT stacd, void *exinf)
{
    MSGTEST_MESSAGE message;
    ER err;

    (void)stacd;
    (void)exinf;

    uart_tx_send("Message receiver task start\r\n");
    while(1) {
        err = tk_rcv_msgq(msgtest_queue_id, &message, TMO_FEVR);
        if(err == E_OK) {
            uart_tx_printf(
                "Message received: sequence=%u value=%d\r\n",
                (UINT)message.sequence,
                message.value
            );
        } else {
            uart_tx_printf("Message receive error: %d\r\n", err);
        }
    }
}
