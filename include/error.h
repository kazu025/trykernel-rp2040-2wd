#ifndef ERROR_H
#define ERROR_H

/**
 * @file error.h
 * @brief TryKernelのエラーコード定義
 *
 * @details
 * カーネルAPIが返す正常終了コードおよびエラーコードを定義する。
 * 正常終了は E_OK(0)、エラーは負の値で表す。
 */

/**
 * @def E_OK
 * @brief 正常終了
 */
#define E_OK        (0)

/**
 * @def E_SYS
 * @brief システムエラー
 */
#define E_SYS       (-5)

/**
 * @def E_NOCOP
 * @brief コプロセッサを使用できない
 */
#define E_NOCOP     (-6)

/**
 * @def E_NOSPT
 * @brief 未サポートの機能
 */
#define E_NOSPT     (-9)

/**
 * @def E_RSFN
 * @brief 予約された機能コード番号
 */
#define E_RSFN      (-10)

/**
 * @def E_RSATR
 * @brief 属性指定が不正
 */
#define E_RSATR     (-11)

/**
 * @def E_PAR
 * @brief パラメータが不正
 */
#define E_PAR       (-17)

/**
 * @def E_ID
 * @brief ID番号が不正
 */
#define E_ID        (-18)

/**
 * @def E_CTX
 * @brief 呼び出しコンテキストが不正
 */
#define E_CTX       (-25)

/**
 * @def E_MACV
 * @brief メモリアクセス違反
 */
#define E_MACV      (-26)

/**
 * @def E_OACV
 * @brief オブジェクトへのアクセス違反
 */
#define E_OACV      (-27)

/**
 * @def E_ILUSE
 * @brief システムコールの使用方法が不正
 */
#define E_ILUSE     (-28)

/**
 * @def E_NOMEM
 * @brief メモリ不足
 */
#define E_NOMEM     (-33)

/**
 * @def E_LIMIT
 * @brief オブジェクト数などの上限超過
 */
#define E_LIMIT     (-34)

/**
 * @def E_OBJ
 * @brief オブジェクトの状態が不正
 */
#define E_OBJ       (-41)

/**
 * @def E_NOEXS
 * @brief 指定されたオブジェクトが存在しない
 */
#define E_NOEXS     (-42)

/**
 * @def E_QOVR
 * @brief キューや資源数が上限を超過
 */
#define E_QOVR      (-43)

/**
 * @def E_RLWAI
 * @brief 待ち状態が強制解除された
 */
#define E_RLWAI     (-49)

/**
 * @def E_TMOUT
 * @brief 待ち時間が経過した
 */
#define E_TMOUT     (-50)

/**
 * @def E_DLT
 * @brief 待ち対象オブジェクトが削除された
 */
#define E_DLT       (-51)

/**
 * @def E_DISWAI
 * @brief 待ち禁止により待ち状態が解除された
 */
#define E_DISWAI    (-52)

/**
 * @def E_IO
 * @brief 入出力エラー
 */
#define E_IO        (-57)

#endif /* ERROR_H */