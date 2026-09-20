#ifndef CONFIG_H
#define CONFIG_H
/* 
 * Try Kernel
 *      コンフィギュレーション定義
*/

#define CNF_MAX_TSKID   32      /* 最大タスク数 */
#define	CNF_MAX_TSKPRI  16      /* 最大タスク優先度 */

#define CNF_MAX_FLGID   8       /* 最大イベントフラグ数 */
#define CNF_MAX_SEMID   8       /* 最大セマフォ数 */
#define CNF_MAX_MTXID   8       /* 最大ミューテックス数 */
#define CNF_MAX_CYCID   8       /* 最大周期ハンドラ数 */
#define CNF_MAX_MSGQID  4       /* 最大メッセージキュー数 */

#endif  /* CONFIG_H */
