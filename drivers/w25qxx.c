#include <trykernel.h>
#include "gpio.h"
#include "spi.h"
#include "w25qxx.h"

#define W25QXX_CS_PIN             17U
#define W25QXX_CMD_JEDEC_ID       0x9FU
#define W25QXX_CMD_READ_STATUS1   0x05U
#define W25QXX_CMD_WRITE_ENABLE   0x06U
#define W25QXX_CMD_WRITE_DISABLE  0x04U
#define W25QXX_CMD_READ_DATA      0x03U
#define W25QXX_CMD_SECTOR_ERASE   0x20U
#define W25QXX_CMD_PAGE_PROGRAM   0x02U
#define W25QXX_DUMMY_DATA         0xFFU
#define W25QXX_BUSY_TIMEOUT_COUNT 1000U

static ID w25qxx_semid;

static BOOL w25qxx_lock(void)
{
    if(w25qxx_semid <= 0) return FALSE;
    return (tk_wai_sem(w25qxx_semid, 1, TMO_FEVR) == E_OK);
}

static BOOL w25qxx_unlock(void)
{
    return (tk_sig_sem(w25qxx_semid, 1) == E_OK);
}

static BOOL w25qxx_send_command(UB command)
{
    UB discard;
    BOOL result;

    gpio_clear(W25QXX_CS_PIN);
    result = spi0_transfer(command, &discard) && spi0_wait_idle();
    gpio_set(W25QXX_CS_PIN);

    return result;
}

void w25qxx_init(void)
{
    gpio_init_out(W25QXX_CS_PIN);
    gpio_set(W25QXX_CS_PIN);
}

ER w25qxx_sync_init(void)
{
    T_CSEM csem = {
        .sematr = TA_TFIFO | TA_FIRST,
        .isemcnt = 1,
        .maxsem = 1,
    };

    w25qxx_semid = tk_cre_sem(&csem);
    if(w25qxx_semid < E_OK) return (ER)w25qxx_semid;
    return E_OK;
}

static BOOL w25qxx_read_jedec_id_unlocked(w25qxx_jedec_id_t *jedec_id)
{
    UB discard;
    BOOL result;

    if(jedec_id == NULL) return FALSE;

    gpio_clear(W25QXX_CS_PIN);
    result = spi0_transfer(W25QXX_CMD_JEDEC_ID, &discard)
        && spi0_transfer(W25QXX_DUMMY_DATA, &jedec_id->manufacturer_id)
        && spi0_transfer(W25QXX_DUMMY_DATA, &jedec_id->memory_type)
        && spi0_transfer(W25QXX_DUMMY_DATA, &jedec_id->capacity_id)
        && spi0_wait_idle();
    gpio_set(W25QXX_CS_PIN);

    return result;
}

static BOOL w25qxx_read_status1_unlocked(UB *status)
{
    UB discard;
    BOOL result;

    if(status == NULL) return FALSE;

    gpio_clear(W25QXX_CS_PIN);
    result = spi0_transfer(W25QXX_CMD_READ_STATUS1, &discard)
        && spi0_transfer(W25QXX_DUMMY_DATA, status)
        && spi0_wait_idle();
    gpio_set(W25QXX_CS_PIN);

    return result;
}

static BOOL w25qxx_wait_ready_unlocked(void)
{
    UB status;
    UW count;

    for(count = 0U; count < W25QXX_BUSY_TIMEOUT_COUNT; count++){
        if(w25qxx_read_status1_unlocked(&status) == FALSE){
            return FALSE;
        }
        if((status & W25QXX_STATUS1_BUSY) == 0U){
            return TRUE;
        }
        /* BUSY中も他のタスクを実行できるよう10ms待つ */
        (void)tk_dly_tsk(10);
    }

    return FALSE;
}

static BOOL w25qxx_write_enable_unlocked(void)
{
    UB status;

    if(w25qxx_wait_ready_unlocked() == FALSE){
        return FALSE;
    }
    if(w25qxx_send_command(W25QXX_CMD_WRITE_ENABLE) == FALSE){
        return FALSE;
    }
    if(w25qxx_read_status1_unlocked(&status) == FALSE){
        return FALSE;
    }

    return ((status & W25QXX_STATUS1_WEL) != 0U);
}

static BOOL w25qxx_write_disable_unlocked(void)
{
    UB status;

    if(w25qxx_send_command(W25QXX_CMD_WRITE_DISABLE) == FALSE){
        return FALSE;
    }
    if(w25qxx_read_status1_unlocked(&status) == FALSE){
        return FALSE;
    }

    return ((status & W25QXX_STATUS1_WEL) == 0U);
}

static BOOL w25qxx_read_unlocked(UW address, UB *data, UINT size)
{
    UB discard;
    UINT i;
    BOOL result = TRUE;

    if((data == NULL) || (size == 0U) || (address > 0x00FFFFFFU)){
        return FALSE;
    }
    if((size - 1U) > (0x00FFFFFFU - address)){
        return FALSE;
    }

    gpio_clear(W25QXX_CS_PIN);
    result = spi0_transfer(W25QXX_CMD_READ_DATA, &discard)
        && spi0_transfer((UB)(address >> 16), &discard)
        && spi0_transfer((UB)(address >> 8), &discard)
        && spi0_transfer((UB)address, &discard);

    for(i = 0U; (i < size) && (result != FALSE); i++){
        result = spi0_transfer(W25QXX_DUMMY_DATA, &data[i]);
    }

    if(result != FALSE){
        result = spi0_wait_idle();
    }
    gpio_set(W25QXX_CS_PIN);

    return result;
}

static BOOL w25qxx_sector_erase_unlocked(UW address)
{
    UB discard;
    BOOL result;

    if((address > 0x00FFFFFFU)
            || ((address & (W25QXX_SECTOR_SIZE - 1U)) != 0U)){
        return FALSE;
    }
    if(w25qxx_write_enable_unlocked() == FALSE){
        return FALSE;
    }

    gpio_clear(W25QXX_CS_PIN);
    result = spi0_transfer(W25QXX_CMD_SECTOR_ERASE, &discard)
        && spi0_transfer((UB)(address >> 16), &discard)
        && spi0_transfer((UB)(address >> 8), &discard)
        && spi0_transfer((UB)address, &discard)
        && spi0_wait_idle();
    gpio_set(W25QXX_CS_PIN);

    if(result == FALSE){
        (void)w25qxx_write_disable_unlocked();
        return FALSE;
    }

    return w25qxx_wait_ready_unlocked();
}

static BOOL w25qxx_page_program_unlocked(
    UW address,
    const UB *data,
    UINT size)
{
    UB discard;
    UINT i;
    UINT page_offset;
    BOOL result;

    if((data == NULL) || (size == 0U) || (size > W25QXX_PAGE_SIZE)
            || (address > 0x00FFFFFFU)){
        return FALSE;
    }

    page_offset = (UINT)(address & (W25QXX_PAGE_SIZE - 1U));
    if(size > (W25QXX_PAGE_SIZE - page_offset)){
        return FALSE;
    }
    if((size - 1U) > (0x00FFFFFFU - address)){
        return FALSE;
    }
    if(w25qxx_write_enable_unlocked() == FALSE){
        return FALSE;
    }

    gpio_clear(W25QXX_CS_PIN);
    result = spi0_transfer(W25QXX_CMD_PAGE_PROGRAM, &discard)
        && spi0_transfer((UB)(address >> 16), &discard)
        && spi0_transfer((UB)(address >> 8), &discard)
        && spi0_transfer((UB)address, &discard);

    for(i = 0U; (i < size) && (result != FALSE); i++){
        result = spi0_transfer(data[i], &discard);
    }

    if(result != FALSE){
        result = spi0_wait_idle();
    }
    gpio_set(W25QXX_CS_PIN);

    if(result == FALSE){
        (void)w25qxx_write_disable_unlocked();
        return FALSE;
    }

    return w25qxx_wait_ready_unlocked();
}

#define W25QXX_LOCKED_CALL(expression) \
    do { \
        BOOL result; \
        if(w25qxx_lock() == FALSE) return FALSE; \
        result = (expression); \
        if(w25qxx_unlock() == FALSE) return FALSE; \
        return result; \
    } while(0)

BOOL w25qxx_read_jedec_id(w25qxx_jedec_id_t *jedec_id)
{
    W25QXX_LOCKED_CALL(w25qxx_read_jedec_id_unlocked(jedec_id));
}

BOOL w25qxx_read_status1(UB *status)
{
    W25QXX_LOCKED_CALL(w25qxx_read_status1_unlocked(status));
}

BOOL w25qxx_wait_ready(void)
{
    W25QXX_LOCKED_CALL(w25qxx_wait_ready_unlocked());
}

BOOL w25qxx_write_enable(void)
{
    W25QXX_LOCKED_CALL(w25qxx_write_enable_unlocked());
}

BOOL w25qxx_write_disable(void)
{
    W25QXX_LOCKED_CALL(w25qxx_write_disable_unlocked());
}

BOOL w25qxx_read(UW address, UB *data, UINT size)
{
    W25QXX_LOCKED_CALL(w25qxx_read_unlocked(address, data, size));
}

BOOL w25qxx_sector_erase(UW address)
{
    W25QXX_LOCKED_CALL(w25qxx_sector_erase_unlocked(address));
}

BOOL w25qxx_page_program(UW address, const UB *data, UINT size)
{
    W25QXX_LOCKED_CALL(w25qxx_page_program_unlocked(address, data, size));
}

const char *w25qxx_manufacturer_name(UB manufacturer_id)
{
    switch(manufacturer_id){
    case 0xEFU:
        return "Winbond";
    case 0xC8U:
        return "GigaDevice";
    case 0xC2U:
        return "Macronix";
    case 0x20U:
        return "Micron";
    case 0x1CU:
        return "Eon";
    default:
        return "Unknown";
    }
}

UW w25qxx_capacity_bytes(UB capacity_id)
{
    if((capacity_id < 0x10U) || (capacity_id > 0x1FU)){
        return 0U;
    }

    return 1UL << capacity_id;
}
