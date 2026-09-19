/*
 * I2C driver for TryKernel / RP2040 I2C0
 *
 * I2C0:
 *   SDA = GPIO4
 *   SCL = GPIO5
 *   Clock = 100 kHz
 */
#include <trykernel.h>
#include <knldef.h>
#include "gpio.h"
#include "i2c.h"

/*
 * clk_sys = 125 MHz、I2C = 100 kHz
 *
 * period = 125000000 / 100000 = 1250
 * High   = 1250 * 2 / 5 = 500
 * Low    = 1250 * 3 / 5 = 750
 */
#define I2C0_FS_SCL_HCNT    500U
#define I2C0_FS_SCL_LCNT    750U
#define I2C0_SDA_HOLD       38U
#define I2C0_FS_SPKLEN      46U

#define I2C0_TIMEOUT_LOOP   100000U

/* I2C0排他制御用FIFOミューテックス */
static ID i2c0_sync_mtxid;
static volatile UW i2c0_error_count;
static volatile UW i2c0_recovery_count;
static volatile UB i2c0_last_error_address;
static volatile UB i2c0_last_error_operation;
static volatile UB i2c0_last_error_stage;
static volatile UW i2c0_last_abort_source;
static UW i2c0_current_abort_source;

#define I2C_ERROR_OP_WRITE       1U
#define I2C_ERROR_OP_WRITE_READ  2U

#define I2C_ERROR_STAGE_DISABLE  1U
#define I2C_ERROR_STAGE_ENABLE   2U
#define I2C_ERROR_STAGE_TX       3U
#define I2C_ERROR_STAGE_RX       4U
#define I2C_ERROR_STAGE_STOP     5U

static BOOL i2c0_set_enabled(BOOL enable);

static void i2c0_recovery_delay(void)
{
    for(volatile UW i = 0U; i < 100U; i++){
        __asm__ volatile("nop");
    }
}

/*
 * SDAを保持したスレーブを解放するためSCLを9回動かし、STOPを生成する。
 * 呼び出し側でI2Cミューテックスを取得していること。
 */
static void i2c0_bus_recover_unlocked(void)
{
    UW pins = (1UL << I2C0_SDA_PIN) | (1UL << I2C0_SCL_PIN);

    (void)i2c0_set_enabled(FALSE);

    out_w(GPIO_CTRL(I2C0_SDA_PIN), GPIO_CTRL_FUNCSEL_SIO);
    out_w(GPIO_CTRL(I2C0_SCL_PIN), GPIO_CTRL_FUNCSEL_SIO);

    /* 出力値はLowに固定し、OEのON/OFFでオープンドレインを模擬する */
    out_w(GPIO_OUT_CLR, pins);
    out_w(GPIO_OE_CLR, pins);
    i2c0_recovery_delay();

    for(UINT i = 0U; i < 9U; i++){
        out_w(GPIO_OE_SET, 1UL << I2C0_SCL_PIN);
        i2c0_recovery_delay();
        out_w(GPIO_OE_CLR, 1UL << I2C0_SCL_PIN);
        i2c0_recovery_delay();
    }

    /* SDA Low → SCL High → SDA HighでSTOP条件を生成する */
    out_w(GPIO_OE_SET, 1UL << I2C0_SDA_PIN);
    i2c0_recovery_delay();
    out_w(GPIO_OE_CLR, 1UL << I2C0_SCL_PIN);
    i2c0_recovery_delay();
    out_w(GPIO_OE_CLR, 1UL << I2C0_SDA_PIN);
    i2c0_recovery_delay();

    i2c0_init();
    i2c0_recovery_count++;
}

/*
 * I2C0排他制御の初期化
 */
ER i2c0_sync_init(void)
{
    T_CMTX cmtx = {
        .mtxatr = TA_INHERIT,
    };

    i2c0_error_count = 0U;
    i2c0_recovery_count = 0U;
    i2c0_last_error_address = 0U;
    i2c0_last_error_operation = 0U;
    i2c0_last_error_stage = 0U;
    i2c0_last_abort_source = 0U;
    i2c0_current_abort_source = 0U;
    i2c0_sync_mtxid = tk_cre_mtx(&cmtx);
    if(i2c0_sync_mtxid < E_OK){
        return (ER)i2c0_sync_mtxid;
    }

    return E_OK;
}

UW i2c0_error_count_get(void)
{
    return i2c0_error_count;
}

UW i2c0_recovery_count_get(void)
{
    return i2c0_recovery_count;
}

UB i2c0_last_error_address_get(void)
{
    return i2c0_last_error_address;
}

UB i2c0_last_error_operation_get(void)
{
    return i2c0_last_error_operation;
}

UB i2c0_last_error_stage_get(void)
{
    return i2c0_last_error_stage;
}

UW i2c0_last_abort_source_get(void)
{
    return i2c0_last_abort_source;
}

static BOOL i2c0_sync_lock(void)
{
    return (tk_loc_mtx(
        i2c0_sync_mtxid,
        TMO_FEVR
    ) == E_OK);
}

static BOOL i2c0_sync_unlock(void)
{
    return (tk_unl_mtx(i2c0_sync_mtxid) == E_OK);
}

/*
 * I2C0の有効／無効を切り替える
 */
static BOOL i2c0_set_enabled(BOOL enable)
{
    UW expected;
    UW count;

    expected = (enable != FALSE) ? I2C_ENABLE_EN : 0U;

    out_w(
        I2C0_BASE + I2Cx_ENABLE,
        expected
    );

    for(count = 0; count < I2C0_TIMEOUT_LOOP; count++){
        if((in_w(I2C0_BASE + I2Cx_ENABLE_STATUS)
                & I2C_ENABLE_EN) == expected){
            return TRUE;
        }
    }

    return FALSE;
}

/*
 * I2C0初期化
 */
void i2c0_init(void)
{
    UW reset_mask;

    reset_mask = RESETS_RESET_I2C0;

    /* I2C0をリセット */
    out_w(
        RESETS_RESET,
        in_w(RESETS_RESET) | reset_mask
    );

    /* I2C0のリセット解除 */
    out_w(
        RESETS_RESET,
        in_w(RESETS_RESET) & ~reset_mask
    );

    while((in_w(RESETS_RESET_DONE) & reset_mask) == 0U);

    /* GPIO4/5をI2C機能に設定 */
    gpio_init_i2c(I2C0_SDA_PIN);
    gpio_init_i2c(I2C0_SCL_PIN);

    (void)i2c0_set_enabled(FALSE);

    /*
     * マスター、Fast mode設定、RESTART有効、
     * スレーブ機能無効
     */
    out_w(
        I2C0_BASE + I2Cx_CON,
        I2C_CON_MASTER_MODE
        | I2C_CON_SPEED_FAST
        | I2C_CON_RESTART_EN
        | I2C_CON_SLAVE_DISABLE
        | I2C_CON_TX_EMPTY_CTRL
    );

    /* 100kHz用SCLタイミング */
    out_w(
        I2C0_BASE + I2Cx_FS_SCL_HCNT,
        I2C0_FS_SCL_HCNT
    );

    out_w(
        I2C0_BASE + I2Cx_FS_SCL_LCNT,
        I2C0_FS_SCL_LCNT
    );

    out_w(
        I2C0_BASE + I2Cx_SDA_HOLD,
        I2C0_SDA_HOLD
    );

    out_w(
        I2C0_BASE + I2Cx_FS_SPKLEN,
        I2C0_FS_SPKLEN
    );

    /* FIFOしきい値 */
    out_w(I2C0_BASE + I2Cx_RX_TL, 0U);
    out_w(I2C0_BASE + I2Cx_TX_TL, 0U);

    /* 今回はポーリング方式なので割込みを使用しない */
    out_w(I2C0_BASE + I2Cx_INTR_MASK, 0U);

    /* 残っている割込み状態をクリア */
    (void)in_w(I2C0_BASE + I2Cx_CLR_INTR);

    (void)i2c0_set_enabled(TRUE);
}
/*
 * 指定した割込み状態になるまで待つ
 */
static BOOL i2c0_wait_raw_status(UW mask)
{
    UW count;
    UW raw_status;

    for(count = 0; count < I2C0_TIMEOUT_LOOP; count++){
        raw_status = in_w(
            I2C0_BASE + I2Cx_RAW_INTR_STAT
        );

        if((raw_status & I2C_RAW_TX_ABRT) != 0U){
            i2c0_current_abort_source = in_w(
                I2C0_BASE + I2Cx_TX_ABRT_SOURCE);
            (void)in_w(
                I2C0_BASE + I2Cx_CLR_TX_ABRT
            );
            return FALSE;
        }

        if((raw_status & mask) != 0U){
            return TRUE;
        }
    }

    return FALSE;
}

/*
 * RX FIFOから1バイト受信するまで待つ
 */
static BOOL i2c0_wait_read_byte(UB *data)
{
    UW count;
    UW raw_status;

    if(data == NULL){
        return FALSE;
    }

    for(count = 0; count < I2C0_TIMEOUT_LOOP; count++){
        raw_status = in_w(
            I2C0_BASE + I2Cx_RAW_INTR_STAT
        );

        if((raw_status & I2C_RAW_TX_ABRT) != 0U){
            i2c0_current_abort_source = in_w(
                I2C0_BASE + I2Cx_TX_ABRT_SOURCE);
            (void)in_w(
                I2C0_BASE + I2Cx_CLR_TX_ABRT
            );
            return FALSE;
        }

        if(in_w(I2C0_BASE + I2Cx_RXFLR) != 0U){
            *data = (UB)(
                in_w(I2C0_BASE + I2Cx_DATA_CMD) & 0xFFU
            );
            return TRUE;
        }
    }

    return FALSE;
}

/*
 * 指定したアドレスへデータを書き込む
 */
static BOOL i2c0_write_unlocked(
    UB addr,
    const UB *data,
    UINT size
)
{
    UINT i;
    UW command;
    BOOL result = FALSE;

    if((addr < 0x08U) || (addr > 0x77U)){
        return FALSE;
    }

    if((data == NULL) || (size == 0U)){
        return FALSE;
    }

    if(i2c0_set_enabled(FALSE) == FALSE){
        i2c0_last_error_stage = I2C_ERROR_STAGE_DISABLE;
        return FALSE;
    }

    /* 前回の割込み状態をクリア */
    (void)in_w(I2C0_BASE + I2Cx_CLR_INTR);

    /* 通信相手を設定 */
    out_w(I2C0_BASE + I2Cx_TAR, (UW)addr);

    if(i2c0_set_enabled(TRUE) == FALSE){
        i2c0_last_error_stage = I2C_ERROR_STAGE_ENABLE;
        return FALSE;
    }

    for(i = 0U; i < size; i++){
        command = (UW)data[i];

        if(i == (size - 1U)){
            command |= I2C_DATA_CMD_STOP;
        }

        out_w(I2C0_BASE + I2Cx_DATA_CMD, command);

        if(i2c0_wait_raw_status(I2C_RAW_TX_EMPTY) == FALSE){
            i2c0_last_error_stage = I2C_ERROR_STAGE_TX;
            goto cleanup;
        }
    }

    if(i2c0_wait_raw_status(I2C_RAW_STOP_DET) == FALSE){
        i2c0_last_error_stage = I2C_ERROR_STAGE_STOP;
        goto cleanup;
    }

    (void)in_w(I2C0_BASE + I2Cx_CLR_STOP_DET);
    result = TRUE;

cleanup:
    (void)in_w(I2C0_BASE + I2Cx_CLR_INTR);
    (void)i2c0_set_enabled(FALSE);

    return result;
}

/*
 * レジスタ番号などを書き込んだ後、
 * Repeated STARTでデータを読み出す
 */
static BOOL i2c0_write_read_unlocked(
    UB addr,
    const UB *write_data,
    UINT write_size,
    UB *read_data,
    UINT read_size
)
{
    UINT i;
    UW command;
    BOOL result = FALSE;

    if((addr < 0x08U) || (addr > 0x77U)){
        return FALSE;
    }

    if((write_data == NULL) || (write_size == 0U)){
        return FALSE;
    }

    if((read_data == NULL) || (read_size == 0U)){
        return FALSE;
    }

    if(i2c0_set_enabled(FALSE) == FALSE){
        i2c0_last_error_stage = I2C_ERROR_STAGE_DISABLE;
        return FALSE;
    }

    /* 前回の割込み状態をクリア */
    (void)in_w(I2C0_BASE + I2Cx_CLR_INTR);

    /* 通信相手を設定 */
    out_w(I2C0_BASE + I2Cx_TAR, (UW)addr);

    if(i2c0_set_enabled(TRUE) == FALSE){
        i2c0_last_error_stage = I2C_ERROR_STAGE_ENABLE;
        return FALSE;
    }

    /*
     * レジスタ番号などを書き込む。
     * 読み出しへ続くためSTOPは生成しない。
     */
    for(i = 0U; i < write_size; i++){
        out_w(
            I2C0_BASE + I2Cx_DATA_CMD,
            (UW)write_data[i]
        );

        if(i2c0_wait_raw_status(
                I2C_RAW_TX_EMPTY) == FALSE){
            i2c0_last_error_stage = I2C_ERROR_STAGE_TX;
            goto cleanup;
        }
    }

    /*
     * 最初の読み出しでRepeated START、
     * 最後の読み出しでSTOPを生成する。
     */
    for(i = 0U; i < read_size; i++){
        command = I2C_DATA_CMD_READ;

        if(i == 0U){
            command |= I2C_DATA_CMD_RESTART;
        }

        if(i == (read_size - 1U)){
            command |= I2C_DATA_CMD_STOP;
        }

        out_w(
            I2C0_BASE + I2Cx_DATA_CMD,
            command
        );

        if(i2c0_wait_read_byte(
                &read_data[i]) == FALSE){
            i2c0_last_error_stage = I2C_ERROR_STAGE_RX;
            goto cleanup;
        }
    }

    if(i2c0_wait_raw_status(
            I2C_RAW_STOP_DET) == FALSE){
        i2c0_last_error_stage = I2C_ERROR_STAGE_STOP;
        goto cleanup;
    }

    (void)in_w(
        I2C0_BASE + I2Cx_CLR_STOP_DET
    );

    result = TRUE;

cleanup:
    /* 残っている割込み状態をクリア */
    (void)in_w(I2C0_BASE + I2Cx_CLR_INTR);
    (void)i2c0_set_enabled(FALSE);

    return result;
}

/*
 * 指定した7ビットアドレスへ
 * 0x00を1バイト書き込み、ACKを確認する
 *
 * Grove LCDの文字表示コントローラは読み出しに対応しないため、
 * 読み出しによるプローブは使用しない。
 */
static BOOL i2c0_probe_unlocked(UB addr)
{
    UW count;
    UW raw_status;
    BOOL stopped = FALSE;
    BOOL aborted = FALSE;

    /* I2C予約アドレスを除外 */
    if((addr < 0x08U) || (addr > 0x77U)){
        return FALSE;
    }

    if(i2c0_set_enabled(FALSE) == FALSE){
        return FALSE;
    }

    /* 前回の割込み状態をクリア */
    (void)in_w(I2C0_BASE + I2Cx_CLR_INTR);

    /* 通信相手のアドレスを設定 */
    out_w(I2C0_BASE + I2Cx_TAR, (UW)addr);

    if(i2c0_set_enabled(TRUE) == FALSE){
        return FALSE;
    }

    /* 0x00を1バイト書き込み後、STOP条件を生成 */
    out_w(
        I2C0_BASE + I2Cx_DATA_CMD,
        0x00U | I2C_DATA_CMD_STOP
    );

    for(count = 0; count < I2C0_TIMEOUT_LOOP; count++){
        raw_status = in_w(I2C0_BASE + I2Cx_RAW_INTR_STAT);

        /* アドレスNACKなどによる送信中断 */
        if((raw_status & I2C_RAW_TX_ABRT) != 0U){
            (void)in_w(I2C0_BASE + I2Cx_CLR_TX_ABRT);
            aborted = TRUE;
        }

        /* STOP条件が完了 */
        if((raw_status & I2C_RAW_STOP_DET) != 0U){
            (void)in_w(I2C0_BASE + I2Cx_CLR_STOP_DET);
            stopped = TRUE;
            break;
        }
    }

    /* ABORT後のSTOPを含む残存状態をクリアする */
    (void)in_w(I2C0_BASE + I2Cx_CLR_INTR);
    (void)i2c0_set_enabled(FALSE);

    return ((aborted == FALSE) && (stopped != FALSE));
}

BOOL i2c0_write(UB addr, const UB *data, UINT size)
{
    BOOL result;

    if(i2c0_sync_lock() == FALSE){
        return FALSE;
    }

    i2c0_current_abort_source = 0U;
    result = i2c0_write_unlocked(addr, data, size);
    if(result == FALSE){
        i2c0_last_error_address = addr;
        i2c0_last_error_operation = I2C_ERROR_OP_WRITE;
        i2c0_last_abort_source = i2c0_current_abort_source;
        i2c0_error_count++;
        i2c0_bus_recover_unlocked();
        result = i2c0_write_unlocked(addr, data, size);
        if(result == FALSE){
            i2c0_error_count++;
        }
    }

    if(i2c0_sync_unlock() == FALSE){
        return FALSE;
    }

    return result;
}

BOOL i2c0_write_read(
    UB addr,
    const UB *write_data,
    UINT write_size,
    UB *read_data,
    UINT read_size
)
{
    BOOL result;

    if(i2c0_sync_lock() == FALSE){
        return FALSE;
    }

    i2c0_current_abort_source = 0U;
    result = i2c0_write_read_unlocked(
        addr,
        write_data,
        write_size,
        read_data,
        read_size
    );
    if(result == FALSE){
        i2c0_last_error_address = addr;
        i2c0_last_error_operation = I2C_ERROR_OP_WRITE_READ;
        i2c0_last_abort_source = i2c0_current_abort_source;
        i2c0_error_count++;
        i2c0_bus_recover_unlocked();
        result = i2c0_write_read_unlocked(
            addr,
            write_data,
            write_size,
            read_data,
            read_size
        );
        if(result == FALSE){
            i2c0_error_count++;
        }
    }

    if(i2c0_sync_unlock() == FALSE){
        return FALSE;
    }

    return result;
}

BOOL i2c0_probe(UB addr)
{
    BOOL result;

    if(i2c0_sync_lock() == FALSE){
        return FALSE;
    }

    result = i2c0_probe_unlocked(addr);

    if(i2c0_sync_unlock() == FALSE){
        return FALSE;
    }

    return result;
}
