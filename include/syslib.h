/**
 * @file syslib.h
 * @brief TryKernel共通システムライブラリ
 *
 * @details
 * メモリマップドI/Oレジスタへの読み書き、
 * レジスタビットのセット・クリア・反転操作、
 * Cortex-Mの割込み状態取得および割込み禁止・許可処理を提供する。
 *
 * 本ファイルの関数は、UART、I2C、SPI、GPIOなどの
 * ハードウェアドライバから使用される。
 */
#ifndef SYSLIB_H
#define SYSLIB_H
/**
 * @brief 32ビットレジスタから値を読み出す
 *
 * @param[in] adr レジスタのメモリアドレス
 *
 * @return レジスタから読み出した32ビット値
 */
static inline UW in_w(UW adr)
{
    return *(_UW*)adr;
}

/**
 * @brief 32ビットレジスタへ値を書き込む
 *
 * @param[in] adr  レジスタのメモリアドレス
 * @param[in] data 書き込む32ビット値
 */
static inline void out_w(UW adr, UW data)
{
    *(_UW*)adr = data;
}

/**
 * @brief レジスタの指定ビットをクリアする
 *
 * @param[in] adr  対象レジスタのメモリアドレス
 * @param[in] data クリアするビットパターン
 */
#define OP_CLR      0x3000
static inline void clr_w(UW adr, UW data)
{
    *(_UW*)(adr + OP_CLR) = data;
}

/**
 * @brief レジスタの指定ビットをセットする
 *
 * @param[in] adr  対象レジスタのメモリアドレス
 * @param[in] data セットするビットパターン
 */
#define OP_SET       0x2000
static inline void set_w(UW adr, UW data)
{
    *(_UW*)(adr + OP_SET) = data;
}

/**
 * @brief レジスタの指定ビットを反転する
 *
 * @param[in] adr  対象レジスタのメモリアドレス
 * @param[in] data 反転するビットパターン
 */
#define OP_XOR      0x1000
static inline void xset_w(UW adr, UW data)
{
    *(_UW*)(adr + OP_XOR) = data;
}
/**
 * @brief PRIMASKレジスタを設定する
 *
 * @param[in] pm 設定する割込みマスク値
 */
static inline void set_primask( INT pm )
{
    __asm__ volatile("msr primask, %0":: "r"(pm));
}
/**
 * @brief PRIMASKレジスタの値を取得する
 *
 * @return PRIMASKレジスタの値
 */
static inline UW get_primask(void)
{
    UW  pm;
    __asm__ volatile("mrs %0, primask": "=r"(pm));
    return pm;
}
/**
 * @brief IPSRレジスタの値を取得する
 *
 * @return IPSRレジスタの値
 */
static inline UW get_ipsr(void)
{
    UW ipsr;
    __asm__ volatile("mrs %0, ipsr" : "=r"(ipsr));
    return ipsr;
}
/**
 * @brief 現在の実行コンテキストが割込み中か判定する
 *
 * @details
 * IPSRが0の場合は通常のタスクコンテキスト、
 * 0以外の場合は例外または割込みコンテキストである。
 *
 * @return TRUE  割込み・例外コンテキスト
 * @return FALSE 通常のタスクコンテキスト
 */
static inline BOOL is_interrupt_context(void)
{
    return(get_ipsr() != 0U);
}
/**
 * @def DI
 * @brief 割込みを禁止し、直前の割込み状態を保存する
 *
 * @param[out] intsts 割込み禁止前のPRIMASK値を保存する変数
 */
#define DI(intsts) (intsts = get_primask(), set_primask(1))

/**
 * @def EI
 * @brief 保存していた割込み状態を復元する
 *
 * @param[in] intsts DIで保存したPRIMASK値
 */
#define EI(intsts) (set_primask(intsts))

#endif  /* STYLIB_H */