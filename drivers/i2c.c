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
 * 1クロックあたりのシステムクロック数:
 *   125000000 / 100000 = 1250
 *
 * SCLのHigh期間:
 *   1250 * 2 / 5 = 500
 *
 * SCLのLow期間:
 *   1250 * 3 / 5 = 750
 */

/**
 * @brief I2C0のSCL High期間。
 *
 * I2CコントローラのSCL_HIGH_COUNTレジスタへ設定する値。
 * システムクロック125MHzを基準に、SCLのHigh期間を設定する。
 */
#define I2C0_FS_SCL_HCNT  500U

/**
 * @brief I2C0のSCL Low期間。
 *
 * I2CコントローラのSCL_LOW_COUNTレジスタへ設定する値。
 * システムクロック125MHzを基準に、SCLのLow期間を設定する。
 */
#define I2C0_FS_SCL_LCNT  750U

/**
 * @brief I2C0のSDA保持時間。
 *
 * SDA信号を保持する時間を設定する値。
 * I2CコントローラのSDA_HOLDレジスタへ設定する。
 */
#define I2C0_SDA_HOLD  38U

/**
 * @brief I2C0のスパイク除去時間。
 *
 * SCLおよびSDA信号に発生する短いノイズパルスを除去するための設定値。
 * I2Cコントローラのスパイクフィルタ設定に使用する。
 */
#define I2C0_FS_SPKLEN  46U

/**
 * @brief I2C通信処理のタイムアウト判定に使用するループ回数。
 *
 * I2Cハードウェアの状態変化を待つ際に、無限ループにならないよう
 * 最大ループ回数を設定する。
 *
 * 実際の待ち時間はCPUクロックやコンパイラ最適化などに依存する。
 */
#define I2C0_TIMEOUT_LOOP  100000U

/**
 * @brief I2C0排他制御用ミューテックスID。
 *
 * 複数のタスクが同時にI2C0を使用しないように保護する。
 */
static ID i2c0_sync_mtxid;

/**
 * @brief I2C通信エラーの発生回数。
 */
static volatile UW i2c0_error_count;

/**
 * @brief I2Cバスリカバリを実行した回数。
 */
static volatile UW i2c0_recovery_count;

/**
 * @brief 最後にエラーが発生したI2Cデバイスのアドレス。
 */
static volatile UB i2c0_last_error_address;

/**
 * @brief 最後に発生したI2C通信操作の種類。
 *
 * 書き込み、書き込み後読み出しなどの操作種別を保持する。
 */
static volatile UB i2c0_last_error_operation;

/**
 * @brief 最後にエラーが発生した通信処理段階。
 *
 * I2C無効化、I2C有効化、送信、受信、STOP検出など、
 * どの処理段階でエラーが起きたかを保持する。
 */
static volatile UB i2c0_last_error_stage;

/**
 * @brief 最後に検出されたI2Cアボート原因。
 *
 * I2Cコントローラが通信を中断した原因を保持する。
 */
static volatile UW i2c0_last_abort_source;

/**
 * @brief 現在処理中のI2Cアボート原因。
 *
 * 通信処理中に取得したアボート原因を一時的に保持し、
 * エラー処理や診断情報の更新に使用する。
 */
static UW i2c0_current_abort_source;

/**
 * @brief I2C書き込み操作を示すエラー操作コード。
 */
#define I2C_ERROR_OP_WRITE       1U

/**
 * @brief I2C書き込み後読み出し操作を示すエラー操作コード。
 *
 * レジスタアドレスを書き込んだ後、続けてデータを読み出す操作を示す。
 */
#define I2C_ERROR_OP_WRITE_READ  2U

/**
 * @brief I2Cハードウェア無効化処理でのエラーを示すコード。
 */
#define I2C_ERROR_STAGE_DISABLE  1U

/**
 * @brief I2Cハードウェア有効化処理でのエラーを示すコード。
 */
#define I2C_ERROR_STAGE_ENABLE   2U

/**
 * @brief I2Cデータ送信処理でのエラーを示すコード。
 */
#define I2C_ERROR_STAGE_TX       3U

/**
 * @brief I2Cデータ受信処理でのエラーを示すコード。
 */
#define I2C_ERROR_STAGE_RX       4U

/**
 * @brief I2C STOP条件検出処理でのエラーを示すコード。
 */
#define I2C_ERROR_STAGE_STOP     5U




static BOOL i2c0_set_enabled(BOOL enable);
/**
 * @brief I2Cバスリカバリ処理用の短い遅延を生成する。
 *
 * @details
 * ループ内でNOP命令を実行し、I2CバスのSCLおよびSDA信号を
 * 切り替える間隔を確保する。
 *
 * ループ変数をvolatileとして扱うことで、コンパイラによる
 * ループの削除や過度な最適化を防ぐ。
 *
 * @note
 * 遅延時間はCPUクロック周波数やコンパイラの生成コードに依存する。
 * この関数はI2Cバスリカバリ処理からのみ使用する内部関数である。
 */
static void i2c0_recovery_delay(void)
{
    for(volatile UW i = 0U; i < 100U; i++){
        __asm__ volatile("nop");
    }
}
/**
 * @brief I2Cバスを手動操作して通信状態を復旧する。
 *
 * @details
 * SDAをLowに保持しているスレーブを解放するため、I2Cコントローラを
 * 一時的に無効化し、SDAおよびSCLをGPIOとして手動制御する。
 *
 * SCLを9回High／Lowに切り替えて、スレーブが保持している通信を
 * 完了させる。その後、次の手順でSTOP条件を生成する。
 *
 * 1. SDAをLowに保持する。
 * 2. SCLをHighへ解放する。
 * 3. SDAをHighへ解放する。
 *
 * GPIOの出力値はLowに固定し、出力イネーブルの設定と解除によって
 * I2Cのオープンドレイン動作を模擬する。
 *
 * 最後にI2C0を再初期化し、バスリカバリ回数を更新する。
 *
 * @note
 * この関数はI2C0のミューテックスを取得した状態で呼び出すこと。
 * 関数内部ではミューテックスの取得および解放を行わない。
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
/**
 * @brief I2C0排他制御用ミューテックスを初期化する。
 *
 * @details
 * I2C0へ同時にアクセスするタスク間の競合を防ぐため、
 * 優先度継承属性（TA_INHERIT）のミューテックスを生成する。
 *
 * あわせて、I2C通信エラー回数、バスリカバリ回数、
 * 最終エラー情報、およびアボート原因の記録を初期化する。
 *
 * @return ER 処理結果。
 * @retval E_OK    ミューテックスの生成と診断情報の初期化に成功した。
 * @retval E_CTX   割込みまたは例外コンテキストから呼び出された。
 * @retval E_PAR   ミューテックス生成情報が不正である。
 * @retval E_RSATR ミューテックス属性が不正である。
 * @retval E_LIMIT 生成可能なミューテックス数の上限に達している。
 *
 * @note
 * この関数は、I2C0を使用する前に一度だけ呼び出す。
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
/**
 * @brief I2C通信エラーの発生回数を取得する。
 *
 * @details
 * I2C0で発生した通信エラーの累計回数を返す。
 *
 * @return UW I2C通信エラーの発生回数。
 */
UW i2c0_error_count_get(void)
{
    return i2c0_error_count;
}
/**
 * @brief I2Cバスリカバリの実行回数を取得する。
 *
 * @details
 * I2C通信エラーなどを検出した際に実行された、
 * I2Cバスリカバリ処理の累計回数を返す。
 *
 * @return UW I2Cバスリカバリの実行回数。
 */
UW i2c0_recovery_count_get(void)
{
    return i2c0_recovery_count;
}
/**
 * @brief 最後にエラーが発生したI2Cデバイスのアドレスを取得する。
 *
 * @details
 * I2C0通信で最後にエラーが発生したときに記録された
 * スレーブアドレスを返す。
 *
 * @return UB 最後にエラーが発生したI2Cデバイスの7ビットアドレス。
 */
UB i2c0_last_error_address_get(void)
{
    return i2c0_last_error_address;
}
/**
 * @brief 最後にエラーが発生したI2C操作の種類を取得する。
 *
 * @details
 * I2C0通信で最後にエラーが発生した際に記録された操作コードを返す。
 * 操作コードは、I2C_ERROR_OP_WRITEやI2C_ERROR_OP_WRITE_READなどで
 * 表される。
 *
 * @return UB 最後にエラーが発生したI2C操作のコード。
 */
UB i2c0_last_error_operation_get(void)
{
    return i2c0_last_error_operation;
}
/**
 * @brief 最後にエラーが発生したI2C処理段階を取得する。
 *
 * @details
 * I2C0通信で最後にエラーが発生した際に記録された処理段階コードを返す。
 * 処理段階コードは、I2C_ERROR_STAGE_DISABLE、I2C_ERROR_STAGE_ENABLE、
 * I2C_ERROR_STAGE_TX、I2C_ERROR_STAGE_RX、I2C_ERROR_STAGE_STOPなどで
 * 表される。
 *
 * @return UB 最後にエラーが発生したI2C処理段階のコード。
 */
UB i2c0_last_error_stage_get(void)
{
    return i2c0_last_error_stage;
}
/**
 * @brief 最後に検出されたI2C通信中断原因を取得する。
 *
 * @details
 * I2C0コントローラが最後に通信を中断した際に記録された
 * アボート原因レジスタの値を返す。
 *
 * @return UW 最後に検出されたI2C通信中断原因。
 */
UW i2c0_last_abort_source_get(void)
{
    return i2c0_last_abort_source;
}
/**
 * @brief I2C0排他制御用ミューテックスを取得する。
 *
 * @details
 * I2C0を使用する前にミューテックスをロックし、
 * 他のタスクがI2C0へ同時にアクセスすることを防ぐ。
 *
 * ミューテックスが他のタスクによって使用中の場合は、
 * 解放されるまで無期限に待機する。
 *
 * @retval TRUE ミューテックスの取得に成功した。
 * @retval FALSE ミューテックスの取得に失敗した。
 *
 * @note
 * i2c0_sync_init()が事前に呼び出されている必要がある。
 * この関数は内部のI2C通信処理から使用する。
 */
static BOOL i2c0_sync_lock(void)
{
    return (tk_loc_mtx(
        i2c0_sync_mtxid,
        TMO_FEVR
    ) == E_OK);
}
/**
 * @brief I2C0排他制御用ミューテックスを解放する。
 *
 * @details
 * I2C0通信処理の終了後にミューテックスを解放し、
 * 他のタスクがI2C0を使用できるようにする。
 *
 * @retval TRUE ミューテックスの解放に成功した。
 * @retval FALSE ミューテックスの解放に失敗した。
 *
 * @note
 * 呼び出し元のタスクが事前にi2c0_sync_lock()で
 * ミューテックスを取得している必要がある。
 */
static BOOL i2c0_sync_unlock(void)
{
    return (tk_unl_mtx(i2c0_sync_mtxid) == E_OK);
}
/**
 * @brief I2C0コントローラを有効または無効にする。
 *
 * @details
 * enableの値に応じてI2C0のENABLEレジスタを設定し、
 * ENABLE_STATUSレジスタの状態が設定値と一致するまでポーリングする。
 *
 * 指定回数以内に状態が一致した場合は成功とし、
 * タイムアウトした場合は失敗とする。
 *
 * @param[in] enable TRUEの場合はI2C0を有効にし、
 *                   FALSEの場合は無効にする。
 *
 * @retval TRUE I2C0の有効／無効状態の変更を確認できた。
 * @retval FALSE 状態変更を確認できず、タイムアウトした。
 *
 * @note
 * 待機時間はI2C0_TIMEOUT_LOOP、CPUクロック、ハードウェアの応答時間に
 * 依存する。この関数はI2Cドライバ内部から使用する。
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
/**
 * @brief I2C0コントローラを初期化する。
 *
 * @details
 * I2C0周辺回路をリセットしてリセット解除を確認した後、
 * SDAおよびSCLに使用するGPIOをI2C機能へ設定する。
 *
 * I2C0は次の設定で初期化する。
 *
 * - マスターモード
 * - Fast mode設定
 * - RESTART条件を有効化
 * - スレーブ機能を無効化
 * - 送信FIFO空状態制御を有効化
 * - SCLタイミングを設定
 * - RX／TX FIFOしきい値を0に設定
 * - I2C割込みを無効化
 *
 * 本実装では割込みを使用せず、I2Cレジスタをポーリングして通信する。
 * 初期化の最後に、残っている割込み状態をクリアしてI2C0を有効化する。
 *
 * @note
 * I2C0のリセット完了を待つ処理にはタイムアウトがない。
 * また、I2C0の有効化に失敗した場合でも、そのエラーは戻り値として
 * 通知されない。
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
/**
 * @brief I2C0の指定した割込み状態が成立するまで待機する。
 *
 * @details
 * I2C0のRAW_INTR_STATレジスタをポーリングし、指定した割込み状態
 * （mask）が発生するまで待機する。
 *
 * 待機中に送信アボート（TX_ABRT）が検出された場合は、
 * TX_ABRT_SOURCEレジスタの内容を保存し、アボート状態をクリアして
 * FALSEを返す。
 *
 * 指定した割込み状態が発生した場合はTRUEを返す。
 * I2C0_TIMEOUT_LOOP回以内に状態が発生しなかった場合は、
 * タイムアウトとしてFALSEを返す。
 *
 * @param[in] mask 待機対象となるI2C割込み状態のビットマスク。
 *
 * @retval TRUE 指定した割込み状態が発生した。
 * @retval FALSE 送信アボートが発生した、またはタイムアウトした。
 *
 * @note
 * 送信アボートが指定した状態より先に検出された場合、
 * 指定した状態を待たずにFALSEを返す。
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
/**
 * @brief I2C0のRX FIFOから1バイト受信するまで待機する。
 *
 * @details
 * I2C0のRAW_INTR_STATレジスタを確認しながら、RX FIFOに受信データが
 * 入るまでポーリングする。
 *
 * 受信待ち中に送信アボート（TX_ABRT）が検出された場合は、
 * TX_ABRT_SOURCEレジスタの内容を保存してアボート状態をクリアし、
 * FALSEを返す。
 *
 * RX FIFOにデータが存在する場合は、DATA_CMDレジスタから1バイトを
 * 読み出してdataへ格納する。
 *
 * @param[out] data 受信した1バイトを格納する領域。
 *
 * @retval TRUE 1バイトの受信に成功した。
 * @retval FALSE dataがNULL、送信アボートが発生、
 *               または受信待ちがタイムアウトした。
 *
 * @note
 * 待ち時間はI2C0_TIMEOUT_LOOPとCPUの動作速度に依存する。
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
/**
 * @brief 指定したI2Cアドレスへデータを書き込む。
 *
 * @details
 * I2C0を一時的に無効化し、通信対象アドレスを設定した後、
 * dataが示すデータをsizeバイト送信する。
 *
 * 各データを書き込んだ後に送信FIFOの状態を確認し、
 * 最後のデータにはSTOP条件を付加する。
 * 送信完了後はSTOP条件の検出を待ち、通信状態をクリアして
 * I2C0を無効化する。
 *
 * 通信中にエラーが発生した場合は、エラーが発生した処理段階を
 * i2c0_last_error_stageへ記録して終了処理へ移行する。
 *
 * @param[in] addr 書き込み先I2Cデバイスの7ビットアドレス。
 *                 0x08～0x77の範囲を指定する。
 * @param[in] data 送信データを格納したバッファ。
 * @param[in] size 送信するバイト数。1以上を指定する。
 *
 * @retval TRUE 指定アドレスへのデータ送信に成功した。
 * @retval FALSE アドレスまたは引数が不正、
 *               I2Cの有効化・無効化に失敗、
 *               データ送信またはSTOP検出がタイムアウトした。
 *
 * @note
 * この関数はI2Cミューテックスを取得した状態で呼び出すこと。
 * 関数内部ではミューテックスの取得および解放を行わない。
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
/**
 * @brief I2Cデバイスへデータを書き込んだ後、データを読み出す。
 *
 * @details
 * write_dataで指定されたデータをI2Cデバイスへ送信した後、
 * STOP条件を生成せずにRepeated START条件を発生させ、
 * 続けてread_sizeバイトを読み出す。
 *
 * 通常は、最初の書き込みデータにレジスタ番号を指定し、
 * そのレジスタからデータを読み出すために使用する。
 *
 * 読み出し処理では、最初の読み出し要求にRepeated STARTを付加し、
 * 最後の読み出し要求にSTOP条件を付加する。
 *
 * 通信中にエラーが発生した場合は、エラーが発生した処理段階を
 * i2c0_last_error_stageへ記録して終了処理を行う。
 *
 * @param[in]  addr       通信対象I2Cデバイスの7ビットアドレス。
 *                        0x08～0x77の範囲を指定する。
 * @param[in]  write_data 書き込むデータを格納したバッファ。
 * @param[in]  write_size 書き込むバイト数。1以上を指定する。
 * @param[out] read_data  読み出したデータを格納するバッファ。
 * @param[in]  read_size  読み出すバイト数。1以上を指定する。
 *
 * @retval TRUE  書き込みと読み出しの両方に成功した。
 * @retval FALSE アドレスまたは引数が不正、
 *               I2Cの有効化・無効化に失敗、
 *               送信、受信、Repeated START、またはSTOP検出に失敗した。
 *
 * @note
 * この関数はI2Cミューテックスを取得した状態で呼び出すこと。
 * 関数内部ではミューテックスの取得および解放を行わない。
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
/**
 * @brief 指定したI2Cデバイスの存在を確認する。
 *
 * @details
 * 指定された7ビットアドレスへ、データ0x00を1バイト送信し、
 * デバイスからACKが返ったかを確認する。
 *
 * Grove LCDの文字表示コントローラは読み出しに対応していないため、
 * 読み出し処理は行わず、書き込みによるプローブを使用する。
 *
 * 通信対象アドレスを設定してデータを送信した後、STOP条件の完了を待つ。
 * アドレスNACKなどによる送信アボートが発生した場合は、
 * デバイスが応答しなかったものとしてFALSEを返す。
 *
 * @param[in] addr 確認対象I2Cデバイスの7ビットアドレス。
 *                 0x08～0x77の範囲を指定する。
 *
 * @retval TRUE  指定アドレスのデバイスがACKを返し、
 *               STOP条件も正常に完了した。
 * @retval FALSE アドレスが予約範囲、I2Cの有効化に失敗、
 *               送信アボートが発生、またはSTOP条件がタイムアウトした。
 *
 * @note
 * この関数はI2Cミューテックスを取得した状態で呼び出すこと。
 * 関数内部ではミューテックスの取得および解放を行わない。
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
/**
 * @brief 指定したI2Cデバイスへデータを書き込む。
 *
 * @details
 * I2C0排他制御用ミューテックスを取得した後、
 * 指定アドレスへsizeバイトのデータを書き込む。
 *
 * 最初の書き込みに失敗した場合は、エラー情報を記録してI2Cバスを
 * リカバリし、同じ書き込みを1回だけ再試行する。
 * 再試行にも失敗した場合は、I2Cエラー回数を追加で更新する。
 *
 * 処理終了後はI2C0のミューテックスを解放する。
 *
 * @param[in] addr 書き込み先I2Cデバイスの7ビットアドレス。
 *                 0x08～0x77の範囲を指定する。
 * @param[in] data 送信するデータを格納したバッファ。
 * @param[in] size 送信するバイト数。1以上を指定する。
 *
 * @retval TRUE  データの書き込みに成功した。
 * @retval FALSE ミューテックスの取得または解放に失敗、
 *               書き込みに失敗し、リトライも失敗した。
 *
 * @note
 * 最初の通信に失敗した場合、I2Cバスリカバリ後に1回だけ再試行する。
 * 通信エラーの詳細は、i2c0_last_error_*()系の関数で取得できる。
 */
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
/**
 * @brief I2Cデバイスへデータを書き込んだ後、データを読み出す。
 *
 * @details
 * I2C0排他制御用ミューテックスを取得し、
 * write_dataで指定されたデータを書き込んだ後、
 * Repeated START条件を使用してread_sizeバイトを読み出す。
 *
 * 最初の通信に失敗した場合は、対象アドレス、操作種別、
 * アボート原因などのエラー情報を記録する。
 * その後、I2Cバスをリカバリして同じ書き込み・読み出し処理を
 * 1回だけ再試行する。
 *
 * 再試行にも失敗した場合は、I2Cエラー回数を追加で更新する。
 * 処理終了後はI2C0のミューテックスを解放する。
 *
 * @param[in]  addr       通信対象I2Cデバイスの7ビットアドレス。
 *                        0x08～0x77の範囲を指定する。
 * @param[in]  write_data 書き込むデータを格納したバッファ。
 * @param[in]  write_size 書き込むバイト数。1以上を指定する。
 * @param[out] read_data  読み出したデータを格納するバッファ。
 * @param[in]  read_size  読み出すバイト数。1以上を指定する。
 *
 * @retval TRUE  書き込みと読み出しの両方に成功した。
 * @retval FALSE ミューテックスの取得または解放に失敗、
 *               通信に失敗し、リトライも失敗した。
 *
 * @note
 * 最初の通信に失敗した場合、I2Cバスリカバリ後に1回だけ再試行する。
 * 通信エラーの詳細は、i2c0_last_error_*()系の関数で取得できる。
 */
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
/**
 * @brief 指定したI2Cデバイスの存在を確認する。
 *
 * @details
 * I2C0排他制御用ミューテックスを取得し、
 * 指定した7ビットアドレスへプローブ通信を行う。
 *
 * プローブではデータ0x00を1バイト送信し、デバイスからのACKと
 * STOP条件の完了を確認する。通信終了後はミューテックスを解放する。
 *
 * @param[in] addr 確認対象I2Cデバイスの7ビットアドレス。
 *                 0x08～0x77の範囲を指定する。
 *
 * @retval TRUE  指定アドレスのデバイスが応答した。
 * @retval FALSE ミューテックスの取得または解放に失敗、
 *               デバイスが応答しない、または通信に失敗した。
 *
 * @note
 * この関数では通信失敗時のI2Cバスリカバリや再試行は行わない。
 */
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
