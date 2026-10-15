/**
 * @file    w25qxx.h
 * @brief   W25QXX SPI Flashドライバのデータ型、定数、APIを宣言する。
 *
 * @details JEDEC情報とステータスビット定義、およびFlashの初期化、同期、
 *          読み出し、消去、書き込み、容量情報取得のインターフェースを公開する。
 */

#ifndef W25QXX_H
#define W25QXX_H

#include <trykernel.h>

/**
 * @brief Flashから読み出したJEDEC識別情報。
 */
typedef struct {
    UB manufacturer_id; //!< JEDECメーカーID。
    UB memory_type;     //!< JEDECメモリー種別ID。
    UB capacity_id;     //!< JEDEC容量ID。
} w25qxx_jedec_id_t;

#define W25QXX_STATUS1_BUSY  (1U << 0) //!< ステータス1: 内部書き込み・消去動作中。
#define W25QXX_STATUS1_WEL   (1U << 1) //!< ステータス1: 書き込み許可ラッチ。
#define W25QXX_STATUS1_BP0   (1U << 2) //!< ステータス1: ブロック保護ビット0。
#define W25QXX_STATUS1_BP1   (1U << 3) //!< ステータス1: ブロック保護ビット1。
#define W25QXX_STATUS1_BP2   (1U << 4) //!< ステータス1: ブロック保護ビット2。
#define W25QXX_STATUS1_TB    (1U << 5) //!< ステータス1: 保護領域の上端・下端選択。
#define W25QXX_STATUS1_SEC   (1U << 6) //!< ステータス1: 保護領域の粒度選択。
#define W25QXX_STATUS1_SRP0  (1U << 7) //!< ステータス1: ステータスレジスター保護ビット。
#define W25QXX_SECTOR_SIZE    4096U    //!< 消去セクターのサイズ（バイト）。
#define W25QXX_PAGE_SIZE      256U     //!< 書き込みページのサイズ（バイト）。

void w25qxx_init(void);
ER w25qxx_sync_init(void);
BOOL w25qxx_read_jedec_id(w25qxx_jedec_id_t *jedec_id);
BOOL w25qxx_read_status1(UB *status);
BOOL w25qxx_wait_ready(void);
BOOL w25qxx_write_enable(void);
BOOL w25qxx_write_disable(void);
BOOL w25qxx_read(UW address, UB *data, UINT size);
BOOL w25qxx_sector_erase(UW address);
BOOL w25qxx_page_program(UW address, const UB *data, UINT size);
const char *w25qxx_manufacturer_name(UB manufacturer_id);
UW w25qxx_capacity_bytes(UB capacity_id);

#endif /* W25QXX_H */
