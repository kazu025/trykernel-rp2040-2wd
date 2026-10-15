/**
 * @file    task_flashlog.c
 * @brief   モーター動作履歴を外部Flashへ記録し、読み出す。
 *
 * @details
 * 動作イベントをメッセージキューで受け取り、W25QXX Flashの予約セクターへ
 * 固定長レコードとして順次保存する。起動時に既存レコードを検査して
 * 書き込み位置を復元し、記録の一覧表示と履歴領域の消去も提供する。
 */

#include <trykernel.h>
#include "task_flashlog.h"
#include "uart_tx.h"
#include "w25qxx.h"

#define FLASHLOG_QUEUE_DEPTH       8                  //!< Flash書き込み要求キューに保持できるメッセージ数。
#define FLASHLOG_MAGIC             0x4D4F544EU        //!< 記録レコードを識別するマジック値。
#define FLASHLOG_RECORD_SIZE       16U                //!< Flashへ保存する1レコードのサイズ（バイト）。
#define FLASHLOG_MAX_RECORDS       (W25QXX_SECTOR_SIZE / FLASHLOG_RECORD_SIZE) //!< 予約セクターに格納できる最大レコード数。

/**
 * @brief Flash書き込みキューで受け渡す動作イベント。
 */
typedef struct {
    FLASHLOG_EVENT event; //!< 記録する動作イベント。
    UW sample_count;       //!< イベント発生時のサンプル数。
} FLASHLOG_MESSAGE;

/**
 * @brief 外部Flashへ保存する固定長の履歴レコード。
 * @details フィールド合計を16バイトとし、magicとeventでレコードの妥当性を確認する。
 */
typedef struct {
    UW magic;          //!< レコード識別値。FLASHLOG_MAGICと一致する。
    UW sequence;       //!< 1から始まるレコード通番。
    UW sample_count;   //!< イベント発生時のサンプル数。
    UB event;          //!< 記録イベントの値。
    UB reserved[3];    //!< レコードサイズを固定するための予約領域。
} FLASHLOG_RECORD;

static FLASHLOG_MESSAGE flashlog_queue_buffer[FLASHLOG_QUEUE_DEPTH]; //!< メッセージキューが使用する静的バッファ。
static ID flashlog_queue_id;                   //!< Flash書き込み要求キューのID。
static volatile BOOL flashlog_ready;            //!< Flashログが初期化され通知を受け付けられる場合にTRUE。
static volatile UW flashlog_base_address;       //!< 履歴保存に使用するFlashセクターの先頭アドレス。
static volatile UW flashlog_record_count;       //!< 保存済みレコード数および次の書き込み位置。
static volatile UW flashlog_write_error_count;  //!< Flash書き込みまたは検証に失敗した回数。
static volatile UW flashlog_queue_overflow_count; //!< 書き込み要求をキューへ登録できなかった回数。

/**
 * @brief 指定サイズのメモリー領域が一致するか比較する。
 * @param[in] a 比較する先頭アドレス。
 * @param[in] b 比較する先頭アドレス。
 * @param[in] size 比較するバイト数。
 * @retval TRUE すべてのバイトが一致する。
 * @retval FALSE 1バイト以上が異なる。
 */
static BOOL bytes_equal(const void *a, const void *b, UINT size)
{
    const UB *left = (const UB *)a;
    const UB *right = (const UB *)b;

    while(size-- > 0U){
        if(*left++ != *right++) return FALSE;
    }
    return TRUE;
}

/**
 * @brief レコード領域が消去済み状態か判定する。
 * @param[in] record 判定するレコード。
 * @retval TRUE 全バイトが0xFFである。
 * @retval FALSE 0xFF以外のバイトが含まれる。
 */
static BOOL record_is_erased(const FLASHLOG_RECORD *record)
{
    const UB *data = (const UB *)record;
    UINT i;

    for(i = 0U; i < sizeof(FLASHLOG_RECORD); i++){
        if(data[i] != 0xFFU) return FALSE;
    }
    return TRUE;
}

/**
 * @brief レコードの識別値とイベント値を検証する。
 * @param[in] record 検証するレコード。
 * @retval TRUE 識別値とイベント値が有効である。
 * @retval FALSE 識別値またはイベント値が不正である。
 */
static BOOL record_is_valid(const FLASHLOG_RECORD *record)
{
    return (record->magic == FLASHLOG_MAGIC)
        && ((record->event == FLASHLOG_EVENT_MOVING)
            || (record->event == FLASHLOG_EVENT_STOPPED));
}

/**
 * @brief イベント値に対応する表示名を取得する。
 * @param[in] event イベント値。
 * @return イベント名の文字列。未定義値の場合は "UNKNOWN"。
 */
static const char *event_name(UB event)
{
    if(event == FLASHLOG_EVENT_MOVING) return "MOVING";
    if(event == FLASHLOG_EVENT_STOPPED) return "STOPPED";
    return "UNKNOWN";
}

/**
 * @brief Flashの履歴領域を走査して記録位置を復元する。
 * @retval TRUE レコード列が正常に読み取れ、書き込み位置を確定できた。
 * @retval FALSE Flash読み出し失敗、容量不正、またはレコード列が不正である。
 * @details Flash末尾の2セクターを予約領域とし、その先頭セクターを走査する。
 *          消去済みレコードに到達した位置を次の書き込み位置として記録する。
 */
static BOOL flashlog_scan(void)
{
    w25qxx_jedec_id_t jedec_id;
    FLASHLOG_RECORD record;
    UW capacity;
    UW index;

    if(sizeof(FLASHLOG_RECORD) != FLASHLOG_RECORD_SIZE) return FALSE;
    if(w25qxx_read_jedec_id(&jedec_id) == FALSE) return FALSE;

    capacity = w25qxx_capacity_bytes(jedec_id.capacity_id);
    if((capacity < (2U * W25QXX_SECTOR_SIZE))
            || (capacity > 0x01000000U)){
        return FALSE;
    }

    flashlog_base_address = capacity - (2U * W25QXX_SECTOR_SIZE);
    flashlog_record_count = 0U;

    for(index = 0U; index < FLASHLOG_MAX_RECORDS; index++){
        if(w25qxx_read(
                flashlog_base_address + (index * FLASHLOG_RECORD_SIZE),
                (UB *)&record,
                sizeof(record)) == FALSE){
            return FALSE;
        }
        if(record_is_erased(&record) != FALSE){
            flashlog_record_count = index;
            return TRUE;
        }
        if((record_is_valid(&record) == FALSE)
                || (record.sequence != (index + 1U))){
            return FALSE;
        }
    }

    flashlog_record_count = FLASHLOG_MAX_RECORDS;
    return TRUE;
}

/**
 * @brief Flashログ用メッセージキューと状態を初期化する。
 * @retval E_OK 初期化に成功した。
 * @return キュー作成に失敗した場合はTRYカーネルのエラーコード。
 * @details Flashの走査と利用可能状態への遷移はログタスクが行う。
 */
ER task_flashlog_init(void)
{
    T_CMSGQ cmsgq;

    cmsgq.msgqatr = TA_TFIFO;
    cmsgq.msgsz = sizeof(FLASHLOG_MESSAGE);
    cmsgq.maxmsg = FLASHLOG_QUEUE_DEPTH;
    cmsgq.bufptr = flashlog_queue_buffer;

    flashlog_queue_id = tk_cre_msgq(&cmsgq);
    if(flashlog_queue_id < E_OK){
        return (ER)flashlog_queue_id;
    }

    flashlog_ready = FALSE;
    flashlog_base_address = 0U;
    flashlog_record_count = 0U;
    flashlog_write_error_count = 0U;
    flashlog_queue_overflow_count = 0U;
    return E_OK;
}

/**
 * @brief 動作イベントをFlashログタスクへ非ブロッキングで通知する。
 * @param[in] event 記録するイベント。MOVINGまたはSTOPPEDを指定する。
 * @param[in] sample_count イベントに関連付けて記録するサンプル数。
 * @retval E_OK キューへの登録に成功した。
 * @retval E_OBJ Flashログが初期化されていない、または利用可能でない。
 * @retval E_PAR イベント値が不正である。
 * @return その他、キュー送信時に発生したTRYカーネルのエラーコード。
 * @details キュー送信に失敗した場合はキューあふれカウンタを加算する。
 */
ER task_flashlog_notify(FLASHLOG_EVENT event, UW sample_count)
{
    FLASHLOG_MESSAGE message;
    ER err;

    if(flashlog_ready == FALSE) return E_OBJ;
    if((event != FLASHLOG_EVENT_MOVING)
            && (event != FLASHLOG_EVENT_STOPPED)){
        return E_PAR;
    }

    message.event = event;
    message.sample_count = sample_count;
    err = tk_snd_msgq(flashlog_queue_id, &message, TMO_POL);
    if(err != E_OK){
        flashlog_queue_overflow_count++;
    }
    return err;
}

/**
 * @brief Flashログの保存セクターを消去し、消去結果を検証する。
 * @retval TRUE セクター消去と全領域の検証に成功した。
 * @retval FALSE Flash情報取得、消去、読み出し、または検証に失敗した。
 * @details 処理開始時にログを利用不可にし、正常完了した場合に記録数を0へ戻して
 *          利用可能状態にする。
 */
BOOL task_flashlog_clear(void)
{
    w25qxx_jedec_id_t jedec_id;
    UB verify_buffer[64];
    UW capacity;
    UW offset;
    UINT i;

    flashlog_ready = FALSE;

    if(w25qxx_read_jedec_id(&jedec_id) == FALSE) return FALSE;
    capacity = w25qxx_capacity_bytes(jedec_id.capacity_id);
    if((capacity < (2U * W25QXX_SECTOR_SIZE))
            || (capacity > 0x01000000U)){
        return FALSE;
    }

    flashlog_base_address = capacity - (2U * W25QXX_SECTOR_SIZE);
    if(w25qxx_sector_erase(flashlog_base_address) == FALSE) return FALSE;

    for(offset = 0U; offset < W25QXX_SECTOR_SIZE;
            offset += sizeof(verify_buffer)){
        if(w25qxx_read(
                flashlog_base_address + offset,
                verify_buffer,
                sizeof(verify_buffer)) == FALSE){
            return FALSE;
        }
        for(i = 0U; i < sizeof(verify_buffer); i++){
            if(verify_buffer[i] != 0xFFU) return FALSE;
        }
    }

    flashlog_record_count = 0U;
    flashlog_ready = TRUE;
    return TRUE;
}

/**
 * @brief Flashに保存された動作履歴とエラーカウンタをUARTへ表示する。
 * @details ログが利用可能であることとFlashが読み出し可能であることを確認し、
 *          保存レコードを順に読み出して表示する。
 */
void task_flashlog_dump(void)
{
    FLASHLOG_RECORD record;
    UW index;
    UW count = flashlog_record_count;

    if(flashlog_ready == FALSE){
        uart_tx_send("Motion flash log is not ready\r\n");
        return;
    }
    if(w25qxx_wait_ready() == FALSE){
        uart_tx_send("Motion flash log read error: flash busy\r\n");
        return;
    }

    uart_tx_printf(
        "Motion flash log: address=0x%x records=%u/%u\r\n",
        (UINT)flashlog_base_address,
        (UINT)count,
        (UINT)FLASHLOG_MAX_RECORDS
    );
    for(index = 0U; index < count; index++){
        if(w25qxx_read(
                flashlog_base_address + (index * FLASHLOG_RECORD_SIZE),
                (UB *)&record,
                sizeof(record)) == FALSE
                || record_is_valid(&record) == FALSE){
            uart_tx_printf("Motion flash log read error: index=%u\r\n", index);
            return;
        }
        uart_tx_printf(
            "  %u: sample=%u event=%s\r\n",
            (UINT)record.sequence,
            (UINT)record.sample_count,
            event_name(record.event)
        );
        /* UART TX taskへ実行権を渡し、一覧表示で送信キューを溢れさせない */
        (void)tk_dly_tsk(10);
    }
    uart_tx_printf(
        "Flash log errors: write=%u queue_overflow=%u\r\n",
        (UINT)flashlog_write_error_count,
        (UINT)flashlog_queue_overflow_count
    );
}

/**
 * @brief Flashログキューを処理し、受信したイベントをFlashへ記録するタスク。
 * @param[in] stacd タスク起動コード（この関数では使用しない）。
 * @param[in] exinf タスク拡張情報（この関数では使用しない）。
 * @details 起動時に既存ログを走査し、無効な場合は再走査を繰り返す。
 *          キューからイベントを受信すると、空き領域へレコードを書き込み、
 *          読み戻しによる検証に成功した場合に記録数を更新する。
 */
void task_flashlog(INT stacd, void *exinf)
{
    FLASHLOG_MESSAGE message;
    FLASHLOG_RECORD record;
    FLASHLOG_RECORD verify;
    UW address;
    ER err;

    (void)stacd;
    (void)exinf;

    if(flashlog_scan() == FALSE){
        uart_tx_send("Motion flash log initialization error\r\n");
        /* 履歴領域が消去されたら自動的に初期化をやり直す */
        while(flashlog_scan() == FALSE){
            (void)tk_dly_tsk(1000);
        }
    }
    flashlog_ready = TRUE;
    uart_tx_printf(
        "Motion flash log task start (0x%x, %u records)\r\n",
        (UINT)flashlog_base_address,
        (UINT)flashlog_record_count
    );

    while(TRUE){
        err = tk_rcv_msgq(flashlog_queue_id, &message, TMO_FEVR);
        if(err != E_OK) continue;

        if(flashlog_record_count >= FLASHLOG_MAX_RECORDS){
            flashlog_write_error_count++;
            continue;
        }

        record.magic = FLASHLOG_MAGIC;
        record.sequence = flashlog_record_count + 1U;
        record.sample_count = message.sample_count;
        record.event = (UB)message.event;
        record.reserved[0] = 0U;
        record.reserved[1] = 0U;
        record.reserved[2] = 0U;
        address = flashlog_base_address
            + (flashlog_record_count * FLASHLOG_RECORD_SIZE);

        if(w25qxx_page_program(address, (const UB *)&record, sizeof(record))
                == FALSE
                || w25qxx_read(address, (UB *)&verify, sizeof(verify)) == FALSE
                || bytes_equal(&record, &verify, sizeof(record)) == FALSE){
            flashlog_write_error_count++;
            continue;
        }

        flashlog_record_count++;
    }
}
