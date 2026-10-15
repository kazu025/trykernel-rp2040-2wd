/**
 * @file    w25qxx.c
 * @brief   W25QXX SPI Flashの基本操作を実装する。
 *
 * @details SPI経由でJEDEC IDやステータスの読み出し、データ読み出し、
 *          セクター消去、ページ書き込みを行う。Flash操作はセマフォで排他し、
 *          書き込み前後の状態確認とBUSY解除待ちを実施する。
 */

#include <trykernel.h>
#include "gpio.h"
#include "spi.h"
#include "w25qxx.h"

#define W25QXX_CS_PIN             17U   //!< W25QXXのチップセレクトに使用するGPIOピン番号。
#define W25QXX_CMD_JEDEC_ID       0x9FU //!< JEDEC ID読み出しコマンド。
#define W25QXX_CMD_READ_STATUS1   0x05U //!< Status Register-1読み出しコマンド。
#define W25QXX_CMD_WRITE_ENABLE   0x06U //!< 書き込み許可コマンド。
#define W25QXX_CMD_WRITE_DISABLE  0x04U //!< 書き込み禁止コマンド。
#define W25QXX_CMD_READ_DATA      0x03U //!< 通常データ読み出しコマンド。
#define W25QXX_CMD_SECTOR_ERASE   0x20U //!< セクター消去コマンド。
#define W25QXX_CMD_PAGE_PROGRAM   0x02U //!< ページ書き込みコマンド。
#define W25QXX_DUMMY_DATA         0xFFU //!< SPI読み出し時に送信するダミーデータ。
#define W25QXX_BUSY_TIMEOUT_COUNT 1000U //!< BUSY解除を確認する最大ポーリング回数。

static ID w25qxx_semid; //!< Flashアクセス排他制御用セマフォID。

/**
 * @brief Flashアクセス用セマフォを取得する。
 * @retval TRUE セマフォを取得した。
 * @retval FALSE セマフォ未初期化、または取得に失敗した。
 */
static BOOL w25qxx_lock(void)
{
    if(w25qxx_semid <= 0) return FALSE;
    return (tk_wai_sem(w25qxx_semid, 1, TMO_FEVR) == E_OK);
}

/**
 * @brief Flashアクセス用セマフォを解放する。
 * @retval TRUE セマフォを解放した。
 * @retval FALSE セマフォ解放に失敗した。
 */
static BOOL w25qxx_unlock(void)
{
    return (tk_sig_sem(w25qxx_semid, 1) == E_OK);
}

/**
 * @brief コマンド1バイトをFlashへ送信する。
 * @param[in] command 送信するFlashコマンド。
 * @retval TRUE SPI送信と転送完了待ちに成功した。
 * @retval FALSE SPI処理に失敗した。
 */
static BOOL w25qxx_send_command(UB command)
{
    UB discard;
    BOOL result;

    gpio_clear(W25QXX_CS_PIN);
    result = spi0_transfer(command, &discard) && spi0_wait_idle();
    gpio_set(W25QXX_CS_PIN);

    return result;
}

/**
 * @brief FlashのチップセレクトGPIOを出力として初期化し、非選択状態にする。
 */
void w25qxx_init(void)
{
    gpio_init_out(W25QXX_CS_PIN);
    gpio_set(W25QXX_CS_PIN);
}

/**
 * @brief Flash操作を排他するバイナリセマフォを生成する。
 * @retval E_OK セマフォを生成した。
 * @return セマフォ作成に失敗した場合はTRYカーネルのエラーコード。
 */
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

/**
 * @brief 排他取得済みの状態でJEDEC IDを読み出す。
 * @param[out] jedec_id メーカー、メモリー種別、容量IDの格納先。
 * @retval TRUE IDを正常に読み出した。
 * @retval FALSE 引数がNULL、またはSPI通信に失敗した。
 */
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

/**
 * @brief 排他取得済みの状態でStatus Register-1を読み出す。
 * @param[out] status ステータス値の格納先。
 * @retval TRUE ステータスを正常に読み出した。
 * @retval FALSE 引数がNULL、またはSPI通信に失敗した。
 */
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

/**
 * @brief 排他取得済みの状態でFlashのBUSY解除を待つ。
 * @retval TRUE FlashがBUSYでなくなった。
 * @retval FALSE ステータス読み出し失敗、または待ち時間超過。
 * @details BUSY中は他タスクへ実行機会を渡すため10 ms待機して再確認する。
 */
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

/**
 * @brief 排他取得済みの状態で書き込み許可を設定し、WELビットを確認する。
 * @retval TRUE WELビットが設定された。
 * @retval FALSE BUSY待ち、コマンド送信、ステータス読み出し、またはWEL確認に失敗した。
 */
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

/**
 * @brief 排他取得済みの状態で書き込み禁止を設定し、WELビットを確認する。
 * @retval TRUE WELビットがクリアされた。
 * @retval FALSE コマンド送信、ステータス読み出し、またはWEL確認に失敗した。
 */
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

/**
 * @brief 排他取得済みの状態で指定範囲のFlashデータを読み出す。
 * @param[in] address 24ビットFlashアドレス。
 * @param[out] data 読み出したデータの格納先。
 * @param[in] size 読み出すバイト数。
 * @retval TRUE 読み出しに成功した。
 * @retval FALSE 引数、アドレス範囲、サイズ、またはSPI通信が不正・失敗である。
 */
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

/**
 * @brief 排他取得済みの状態で指定セクターを消去する。
 * @param[in] address 消去するセクターの先頭アドレス。
 * @retval TRUE セクター消去が完了した。
 * @retval FALSE アドレス不正、書き込み許可失敗、SPI通信失敗、またはBUSY待ち失敗。
 */
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

/**
 * @brief 排他取得済みの状態でFlashの1ページ内へデータを書き込む。
 * @param[in] address 24ビットFlashアドレス。
 * @param[in] data 書き込むデータ。
 * @param[in] size 書き込むバイト数。1ページ境界をまたがないこと。
 * @retval TRUE ページ書き込みが完了した。
 * @retval FALSE 引数、アドレス範囲、ページ境界、書き込み許可、SPI通信、
 *               またはBUSY待ちに問題がある。
 */
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

/** 公開APIでセマフォ取得・操作・解放を行う共通マクロ。 */
#define W25QXX_LOCKED_CALL(expression) \
    do { \
        BOOL result; \
        if(w25qxx_lock() == FALSE) return FALSE; \
        result = (expression); \
        if(w25qxx_unlock() == FALSE) return FALSE; \
        return result; \
    } while(0)

/**
 * @brief FlashのJEDEC IDを排他制御付きで読み出す。
 * @param[out] jedec_id メーカー、メモリー種別、容量IDの格納先。
 * @retval TRUE 読み出しに成功した。
 * @retval FALSE セマフォまたはSPI操作に失敗した。
 */
BOOL w25qxx_read_jedec_id(w25qxx_jedec_id_t *jedec_id)
{
    W25QXX_LOCKED_CALL(w25qxx_read_jedec_id_unlocked(jedec_id));
}

/**
 * @brief Status Register-1を排他制御付きで読み出す。
 * @param[out] status ステータス値の格納先。
 * @retval TRUE 読み出しに成功した。
 * @retval FALSE セマフォまたはSPI操作に失敗した。
 */
BOOL w25qxx_read_status1(UB *status)
{
    W25QXX_LOCKED_CALL(w25qxx_read_status1_unlocked(status));
}

/**
 * @brief FlashのBUSY解除を排他制御付きで待つ。
 * @retval TRUE Flashが使用可能になった。
 * @retval FALSE セマフォ取得、ステータス読み出し、または待ち時間超過。
 */
BOOL w25qxx_wait_ready(void)
{
    W25QXX_LOCKED_CALL(w25qxx_wait_ready_unlocked());
}

/**
 * @brief Flashの書き込み許可を設定し、WEL状態を確認する。
 * @retval TRUE 書き込み許可状態になった。
 * @retval FALSE セマフォまたはFlash操作に失敗した。
 */
BOOL w25qxx_write_enable(void)
{
    W25QXX_LOCKED_CALL(w25qxx_write_enable_unlocked());
}

/**
 * @brief Flashの書き込み禁止を設定し、WEL状態を確認する。
 * @retval TRUE 書き込み禁止状態になった。
 * @retval FALSE セマフォまたはFlash操作に失敗した。
 */
BOOL w25qxx_write_disable(void)
{
    W25QXX_LOCKED_CALL(w25qxx_write_disable_unlocked());
}

/**
 * @brief Flashから指定範囲のデータを排他制御付きで読み出す。
 * @param[in] address 読み出し開始アドレス。
 * @param[out] data 読み出したデータの格納先。
 * @param[in] size 読み出すバイト数。
 * @retval TRUE 読み出しに成功した。
 * @retval FALSE 引数、アドレス範囲、または通信処理に問題がある。
 */
BOOL w25qxx_read(UW address, UB *data, UINT size)
{
    W25QXX_LOCKED_CALL(w25qxx_read_unlocked(address, data, size));
}

/**
 * @brief 指定アドレスのセクターを排他制御付きで消去する。
 * @param[in] address 消去するセクターの先頭アドレス（セクター境界に整列すること）。
 * @retval TRUE 消去に成功した。
 * @retval FALSE アドレス不正、セマフォまたはFlash操作に失敗した。
 */
BOOL w25qxx_sector_erase(UW address)
{
    W25QXX_LOCKED_CALL(w25qxx_sector_erase_unlocked(address));
}

/**
 * @brief 指定アドレスからFlashの1ページ内へデータを書き込む。
 * @param[in] address 書き込み先アドレス。
 * @param[in] data 書き込むデータ。
 * @param[in] size 書き込むバイト数。ページ境界をまたがないこと。
 * @retval TRUE 書き込みに成功した。
 * @retval FALSE 引数、ページ境界、セマフォ、またはFlash操作に問題がある。
 */
BOOL w25qxx_page_program(UW address, const UB *data, UINT size)
{
    W25QXX_LOCKED_CALL(w25qxx_page_program_unlocked(address, data, size));
}

/**
 * @brief JEDECメーカーIDに対応するメーカー名を取得する。
 * @param[in] manufacturer_id JEDEC IDのメーカーコード。
 * @return メーカー名の文字列。未対応IDの場合は"Unknown"。
 */
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

/**
 * @brief JEDEC容量IDからFlash容量をバイト単位で求める。
 * @param[in] capacity_id JEDEC IDの容量コード。
 * @return 容量（バイト）。対応範囲外の場合は0。
 */
UW w25qxx_capacity_bytes(UB capacity_id)
{
    if((capacity_id < 0x10U) || (capacity_id > 0x1FU)){
        return 0U;
    }

    return 1UL << capacity_id;
}
