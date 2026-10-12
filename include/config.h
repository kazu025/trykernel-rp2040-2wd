/**
 * @file    config.h
 * @brief   TryKernelの構成上限を定義する。
 *
 * @details
 * TryKernelが管理できるタスク、優先度、
 * イベントフラグ、セマフォ、ミューテックス、
 * 周期ハンドラ、メッセージキューの最大数を定義する。
 *
 * これらの値は、各カーネルオブジェクトの管理テーブルや
 * レディキューの配列サイズに使用される。
 *
 * 設定値を変更すると、必要な管理メモリ量や、
 * 生成可能なカーネルオブジェクト数が変化する。
 */

#ifndef CONFIG_H
#define CONFIG_H

/**
 * @brief 生成可能な最大タスク数。
 */
#define CNF_MAX_TSKID 32

/**
 * @brief 使用可能な最大タスク優先度数。
 *
 * 優先度1からCNF_MAX_TSKPRIまでを使用する。
 */
#define CNF_MAX_TSKPRI 16

/**
 * @brief 生成可能な最大イベントフラグ数。
 */
#define CNF_MAX_FLGID 8

/**
 * @brief 生成可能な最大セマフォ数。
 */
#define CNF_MAX_SEMID 8

/**
 * @brief 生成可能な最大ミューテックス数。
 */
#define CNF_MAX_MTXID 8

/**
 * @brief 生成可能な最大周期ハンドラ数。
 */
#define CNF_MAX_CYCID 8

/**
 * @brief 生成可能な最大メッセージキュー数。
 */
#define CNF_MAX_MSGQID 4

#endif /* CONFIG_H */

