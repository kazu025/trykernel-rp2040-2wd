/**
 * @file    apidef.h
 * @brief   TryKernelの公開API、属性、生成情報を定義する。
 *
 * @details
 * タスク管理、タスク付属同期、イベントフラグ、セマフォ、
 * ミューテックス、固定長メッセージキュー、周期ハンドラで使用する
 * APIとデータ型、定数を宣言する。
 *
 * 本ファイルでは、各カーネルオブジェクトの生成情報を表す構造体と、
 * オブジェクトを操作するためのAPI関数を定義する。
 *
 * 主な定義は次のとおり。
 *
 * - タスク生成・起動・終了
 * - タスク遅延、起床待ち、タスク起床
 * - イベントフラグの生成、設定、クリア、待ち
 * - セマフォの生成、資源獲得、資源返却
 * - ミューテックスの生成、ロック、アンロック
 * - 固定長メッセージキューの生成、送信、受信
 * - 周期ハンドラの生成、開始、停止、削除、状態参照
 *
 * 待ち時間には、待機なしを示すTMO_POLと、
 * 無期限待機を示すTMO_FEVRを使用する。
 *
 * @note
 * このファイルはカーネルAPIの宣言をまとめたヘッダであり、
 * APIの実装はkernelディレクトリ内の各ソースファイルに定義される。
 */

#ifndef APIDEF_H
#define APIDEF_H

/**
 * @name 待ち時間定義
 * @{
 */

/** 待ち時間なし。 */
#define TMO_POL (0)

/** 無期限待機。 */
#define TMO_FEVR (-1)

/** @} */

/**
 * @name 待ち関連属性
 * @{
 */

/**
 * @brief 待ちタスクをFIFO順で管理する。
 *
 * ミューテックスでは優先度継承を行わない。
 */
#define TA_TFIFO 0x00000000U

/**
 * @brief 待ち行列の先頭タスクを優先する。
 *
 * セマフォの待ち属性として使用する。
 */
#define TA_FIRST 0x00000000U

/**
 * @brief 待ちタスクを優先度順で管理する。
 */
#define TA_TPRI 0x00000001U

/**
 * @brief 要求資源数の少ないタスクを優先する。
 */
#define TA_CNT 0x00000002U

/**
 * @brief ミューテックスの優先度継承プロトコルを使用する。
 */
#define TA_INHERIT 0x00000003U

/**
 * @brief 複数タスクの同時待ちを許可する。
 *
 * イベントフラグに対して複数のタスクが同時に待機できることを示す。
 * 現在の実装では、TA_WMULの有無にかかわらず複数タスクの待ちを許可する。
 */
#define TA_WMUL 0x00000008U

/** @} */

/**
 * @brief タスク生成情報。
 */
typedef struct {
    ATR  tskatr;  /**< タスク属性。 */
    FP   task;     /**< タスク開始関数のアドレス。 */
    PRI  itskpri;  /**< タスクの初期優先度。 */
    SZ   stksz;    /**< タスクスタックのサイズ。 */
    void *bufptr;  /**< タスクスタック領域へのポインタ。 */
} T_CTSK;

/**
 * @brief タスク優先度からレディキューのインデックスを求める。
 *
 * @param[in] pri タスク優先度。
 * @return UINT レディキューのインデックス。
 */
#define PRI_INDEX(pri) ((UINT)(pri) - 1U)

/**
 * @name タスク属性
 * @{
 */

/** タスクが高級言語で記述されていることを示す。 */
#define TA_HLNG 0x00000001U

/** ユーザが指定したバッファをタスクスタックとして使用する。 */
#define TA_USERBUF 0x00000020U

/** 保護レベル0。 */
#define TA_RNG0 0x00000000U

/** 保護レベル1。 */
#define TA_RNG1 0x00000100U

/** 保護レベル2。 */
#define TA_RNG2 0x00000200U

/** 保護レベル3。 */
#define TA_RNG3 0x00000300U

/** @} */

/**
 * @name タスク管理API
 * @{
 */

/** タスクを生成する。 */
ID tk_cre_tsk(const T_CTSK *pk_ctsk);

/** 生成済みタスクを起動する。 */
ER tk_sta_tsk(ID tskid, INT stacd);

/** 現在実行中のタスクを終了する。 */
void tk_ext_tsk(void);

/** @} */

/**
 * @name タスク付属同期API
 * @{
 */

/** 現在のタスクを指定時間だけ遅延させる。 */
ER tk_dly_tsk(RELTIM dlytim);

/** 現在のタスクを起床要求が来るまで待機させる。 */
ER tk_slp_tsk(TMO tmout);

/** 指定したタスクへ起床要求を送る。 */
ER tk_wup_tsk(ID tskid);

/** @} */

/**
 * @brief イベントフラグ生成情報。
 */
typedef struct t_cflg {
    ATR  flgatr;   /**< イベントフラグ属性。 */
    UINT iflgptn;  /**< 初期イベントフラグ値。 */
} T_CFLG;

/**
 * @name イベントフラグAPI
 * @{
 */

/** イベントフラグを生成する。 */
ID tk_cre_flg(const T_CFLG *pk_cflg);

/** イベントフラグを設定する。 */
ER tk_set_flg(ID flgid, UINT setptn);

/** イベントフラグをクリアする。 */
ER tk_clr_flg(ID flgid, UINT clrptn);

/** @} */

/**
 * @brief イベントフラグをすべて満たすまで待つ。
 */
#define TWF_ANDW 0x00000000U

/**
 * @brief 指定したイベントフラグのいずれかを待つ。
 */
#define TWF_ORW 0x00000001U

/**
 * @brief 待ち解除時にすべてのイベントフラグをクリアする。
 */
#define TWF_CLR 0x00000010U

/**
 * @brief 待ち解除時に待ち合わせたビットだけをクリアする。
 */
#define TWF_BITCLR 0x00000020U

/** イベントフラグの条件成立まで待つ。 */
ER tk_wai_flg(
    ID flgid,
    UINT waiptn,
    UINT wfmode,
    UINT *p_flgptn,
    TMO tmout
);

/**
 * @brief セマフォ生成情報。
 */
typedef struct t_csem {
    ATR sematr;  /**< セマフォ属性。 */
    INT isemcnt; /**< セマフォ資源数の初期値。 */
    INT maxsem;  /**< セマフォ資源数の最大値。 */
} T_CSEM;

/**
 * @name セマフォAPI
 * @{
 */

/** セマフォを生成する。 */
ID tk_cre_sem(const T_CSEM *pk_csem);

/** セマフォへ資源を返却する。 */
ER tk_sig_sem(ID semid, INT cnt);

/** セマフォから資源を獲得する。 */
ER tk_wai_sem(ID semid, INT cnt, TMO tmout);

/** @} */

/**
 * @brief ミューテックス生成情報。
 *
 * 再帰ロックには対応していない。
 */
typedef struct {
    ATR mtxatr; /**< ミューテックス属性。 */
} T_CMTX;

/**
 * @name ミューテックスAPI
 * @{
 */

/** ミューテックスを生成する。 */
ID tk_cre_mtx(const T_CMTX *pk_cmtx);

/** ミューテックスを取得する。 */
ER tk_loc_mtx(ID mtxid, TMO tmout);

/** ミューテックスを解放する。 */
ER tk_unl_mtx(ID mtxid);

/** @} */

/**
 * @brief 固定長メッセージキュー生成情報。
 */
typedef struct t_cmsgq {
    ATR  msgqatr; /**< メッセージキュー属性。 */
    INT  msgsz;   /**< 1メッセージあたりのサイズ。 */
    INT  maxmsg;  /**< 格納できる最大メッセージ数。 */
    void *bufptr; /**< メッセージ格納領域へのポインタ。 */
} T_CMSGQ;

/**
 * @name 固定長メッセージキューAPI
 * @{
 */

/** 固定長メッセージキューを生成する。 */
ID tk_cre_msgq(const T_CMSGQ *pk_cmsgq);

/** メッセージキューへメッセージを送信する。 */
ER tk_snd_msgq(ID msgqid, const void *msg, TMO tmout);

/** メッセージキューからメッセージを受信する。 */
ER tk_rcv_msgq(ID msgqid, void *msg, TMO tmout);

/** @} */

/**
 * @brief 周期ハンドラを生成直後から開始する属性。
 */
#define TA_STA 0x00000002U

/**
 * @brief 周期ハンドラが停止中であることを示す。
 */
#define TCYC_STP 0U

/**
 * @brief 周期ハンドラが開始中であることを示す。
 */
#define TCYC_STA 1U

/**
 * @brief 周期ハンドラ生成情報。
 *
 * 周期時間と初回遅延時間の単位はミリ秒である。
 */
typedef struct {
    void *exinf;              /**< コールバックへ渡す拡張情報。 */
    ATR cycatr;               /**< 周期ハンドラ属性。TA_HLNG、TA_STAなど。 */
    void (*cychdr)(void *);   /**< 周期満了時に呼び出すコールバック関数。 */
    RELTIM cyctim;            /**< 周期。0より大きい値を指定する。 */
    RELTIM cycphs;            /**< 初回実行までの遅延時間。0以上を指定する。 */
} T_CCYC;

/**
 * @brief 周期ハンドラ状態参照情報。
 */
typedef struct {
    UINT cycstat;  /**< 周期ハンドラの状態。TCYC_STPまたはTCYC_STA。 */
    RELTIM lfttim; /**< 次回実行までの残り時間。停止中は0。 */
} T_RCYC;

/**
 * @name 周期ハンドラAPI
 *
 * 周期ハンドラの管理APIはタスクコンテキストから呼び出す。
 * @{
 */

/** 周期ハンドラを生成する。 */
ID tk_cre_cyc(const T_CCYC *pk_ccyc);

/** 周期ハンドラを開始する。 */
ER tk_sta_cyc(ID cycid);

/** 周期ハンドラを停止する。 */
ER tk_stp_cyc(ID cycid);

/** 周期ハンドラを削除する。 */
ER tk_del_cyc(ID cycid);

/** 周期ハンドラの状態を参照する。 */
ER tk_ref_cyc(ID cycid, T_RCYC *pk_rcyc);

/** @} */

#endif  /* APIDEF_H */
