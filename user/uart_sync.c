#include <trykernel.h>
#include "uart_sync.h"

static ID uart_sync_semid; //!< UART 操船制御用セマフォID

/**
 * @brief UART送信排他制御用セマフォを初期化する。
 *
 * @details
 * UART送信処理の排他制御に使用するバイナリセマフォを生成する。
 * セマフォの初期資源数と最大資源数は1に設定し、
 * 待ちタスクはFIFO順で処理する。
 *
 * @return ER エラーコード。
 * @retval E_OK セマフォの生成に成功した。
 * @retval E_LIMIT セマフォを生成できなかった。
 * @retval E_PAR セマフォ設定に問題があった。
 */
ER uart_sync_init(void){
    /// Uart送信排他制御用のセマフォ設定
    /// UART送信処理は同時に１だけ実行できる
    T_CSEM csem = {
        /**
         * セマフォ獲得を待つタスクをFIFO順位する。
         * 待行列の先頭タスクを優先する
         */
        .sematr = TA_TFIFO | TA_FIRST,
        .isemcnt = 1,   //!< セマフォ資源数
        .maxsem  = 1,   //!< セマフォ資源数の最大値(実質的にバイナリセマフォ)
    };
    uart_sync_semid = tk_cre_sem(&csem);
    if(uart_sync_semid < E_OK){
        return (ER)uart_sync_semid;
    }
    return E_OK;
}
/**
 * @brief UART送信権を取得する。
 *
 * @details
 * UART送信排他制御用セマフォから1つの資源を取得する。
 * セマフォが使用中の場合は、解放されるまで無期限に待機する。
 *
 * @return ER エラーコード。
 * @retval E_OK UART送信権の取得に成功した。
 * @retval E_CTX 割込みまたは例外コンテキストから呼び出された。
 * @retval E_ID セマフォIDが不正である。
 * @retval E_NOEXS 指定されたセマフォが存在しない。
 * @retval E_TMOUT 待ち時間が経過した。
 */
ER uart_sync_lock(void){
    return tk_wai_sem(uart_sync_semid, 1, TMO_FEVR);
}
/**
 * @brief UART送信権を解放する。
 *
 * @details
 * UART送信排他制御用セマフォへ資源を1つ返却する。
 * UART送信権を取得している処理が終了した後に呼び出す。
 *
 * @return ER エラーコード。
 * @retval E_OK UART送信権の解放に成功した。
 * @retval E_ID セマフォIDが不正である。
 * @retval E_NOEXS 指定されたセマフォが存在しない。
 * @retval E_QOVR セマフォの資源数が最大値を超える。
 */
ER uart_sync_unlock(void){
    return tk_sig_sem(uart_sync_semid, 1);
}
