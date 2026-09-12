#include <trykernel.h>
#include "task_flashlog.h"
#include "uart_tx.h"
#include "w25qxx.h"

#define FLASHLOG_QUEUE_DEPTH       8
#define FLASHLOG_MAGIC             0x4D4F544EU
#define FLASHLOG_RECORD_SIZE       16U
#define FLASHLOG_MAX_RECORDS       (W25QXX_SECTOR_SIZE / FLASHLOG_RECORD_SIZE)

typedef struct {
    FLASHLOG_EVENT event;
    UW sample_count;
} FLASHLOG_MESSAGE;

typedef struct {
    UW magic;
    UW sequence;
    UW sample_count;
    UB event;
    UB reserved[3];
} FLASHLOG_RECORD;

static FLASHLOG_MESSAGE flashlog_queue_buffer[FLASHLOG_QUEUE_DEPTH];
static ID flashlog_queue_id;
static volatile BOOL flashlog_ready;
static volatile UW flashlog_base_address;
static volatile UW flashlog_record_count;
static volatile UW flashlog_write_error_count;
static volatile UW flashlog_queue_overflow_count;

static BOOL bytes_equal(const void *a, const void *b, UINT size)
{
    const UB *left = (const UB *)a;
    const UB *right = (const UB *)b;

    while(size-- > 0U){
        if(*left++ != *right++) return FALSE;
    }
    return TRUE;
}

static BOOL record_is_erased(const FLASHLOG_RECORD *record)
{
    const UB *data = (const UB *)record;
    UINT i;

    for(i = 0U; i < sizeof(FLASHLOG_RECORD); i++){
        if(data[i] != 0xFFU) return FALSE;
    }
    return TRUE;
}

static BOOL record_is_valid(const FLASHLOG_RECORD *record)
{
    return (record->magic == FLASHLOG_MAGIC)
        && ((record->event == FLASHLOG_EVENT_MOVING)
            || (record->event == FLASHLOG_EVENT_STOPPED));
}

static const char *event_name(UB event)
{
    if(event == FLASHLOG_EVENT_MOVING) return "MOVING";
    if(event == FLASHLOG_EVENT_STOPPED) return "STOPPED";
    return "UNKNOWN";
}

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
