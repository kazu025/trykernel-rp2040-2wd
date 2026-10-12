
/**
 * @file    knldef.h
 * @brief   TryKernelカーネル内部の共通定義。
 *
 * @details
 * タスク管理、待ち状態、タスク管理ブロック（TCB）、
 * カーネルオブジェクト管理ブロック、レディキュー、
 * ウェイトキュー、ディスパッチャおよびスケジューラで
 * 使用する型、変数、関数を定義する。
 *
 * 本ファイルはカーネル内部で共有する定義をまとめたものであり、
 * タスク管理や同期機構などのカーネル実装から参照される。
 *
 * @note
 * 一般のアプリケーションから直接使用するAPI定義ではなく、
 * TryKernelカーネル内部向けのヘッダである。
 */

#ifndef KNLDEF_H
#define KNLDEF_H

/**
 * @brief タスクの状態。
 */
typedef enum {
    TS_NONEXIST = 0,    /**< 未登録状態。 */
    TS_READY    = 1,    /**< 実行中、または実行可能状態。 */
    TS_WAIT     = 2,    /**< 待ち状態。 */
    TS_DORMANT  = 8     /**< 休止状態。 */
} TSTAT;

/**
 * @brief タスクが待ち状態になった理由。
 */
typedef enum {
    TWFCT_NON      = 0, /**< 待ち状態ではない。 */
    TWFCT_DLY      = 1, /**< tk_dly_tsk()による時間待ち。 */
    TWFCT_SLP      = 2, /**< tk_slp_tsk()による起床待ち。 */
    TWFCT_FLG      = 3, /**< tk_wai_flg()によるイベントフラグ待ち。 */
    TWFCT_SEM      = 4, /**< tk_wai_sem()によるセマフォ資源待ち。 */
    TWFCT_SND_MSGQ = 5, /**< tk_snd_msgq()によるメッセージ送信待ち。 */
    TWFCT_MTX      = 7, /**< tk_loc_mtx()によるミューテックス待ち。 */
    TWFCT_RCV_MSGQ = 6  /**< tk_rcv_msgq()によるメッセージ受信待ち。 */
} TWFCT;

/**
 * @brief タスク管理ブロック（Task Control Block）。
 *
 * @details
 * タスクの実行コンテキスト、状態、優先度、待ち状態、
 * および同期オブジェクトに関する情報を保持する。
 */
typedef struct st_tcb {
    /** 保存されたタスクの実行コンテキストへのポインタ。 */
    void *context;

    /** タスクキュー上で1つ前のTCB。 */
    struct st_tcb *pre;

    /** タスクキュー上で1つ後のTCB。 */
    struct st_tcb *next;

    /** タスクの状態。 */
    TSTAT state;

    /** タスクの実行開始アドレス。 */
    FP tskadr;

    /** タスク生成時に設定された基準優先度。 */
    PRI btskpri;

    /** 優先度継承などを反映した現在の実行優先度。 */
    PRI itskpri;

    /** タスクスタックの先頭アドレス。 */
    void *stkadr;

    /** タスクスタックのサイズ。 */
    SZ stksz;

    /** タスクに対する起床要求の保留数。 */
    INT wupcnt;

    /** タスクが待ち状態になった理由。 */
    TWFCT waifct;

    /** 待ち対象となるカーネルオブジェクトのID。 */
    ID waiobj;

    /** 残りの待ち時間。 */
    RELTIM waitim;

    /** 待ち解除時の結果を書き込むエラーコードへのポインタ。 */
    ER *waierr;

    /** 待ち合わせるイベントフラグのビットパターン。 */
    UINT waiptn;

    /** イベントフラグの待ちモード。 */
    UINT wfmode;

    /** 待ち解除時に取得したイベントフラグ値の格納先。 */
    UINT *p_flgptn;

    /** セマフォから獲得する資源数。 */
    INT waisem;

    /** メッセージ送信待ちのメッセージへのポインタ。 */
    const void *sndmsg;

    /** 受信メッセージの格納先バッファ。 */
    void *rcvmsg;
} TCB;

/** タスク管理ブロックの配列。 */
extern TCB tcb_tbl[];

/** 優先度ごとのレディキュー。 */
extern TCB *ready_queue[];

/** 現在実行中のタスク。 */
extern TCB *cur_task;

/** スケジューラが選択した次の実行タスク。 */
extern TCB *sche_task;

/** 待ち状態にあるタスクのキュー。 */
extern TCB *wait_queue;

/** システムリセット後に実行されるリセットハンドラ。 */
extern void Reset_Handler(void);

/** PendSVから呼び出されるディスパッチャ。 */
extern void dispatch_entry(void);

/** SysTick割込みから呼び出されるシステムタイマハンドラ。 */
extern void systimer_handler(void);

/**
 * @brief システム制御ブロックのICSRレジスタ。
 *
 * PendSV要求など、プロセッサの例外状態を制御する。
 */
#define SCB_ICSR 0xE000ED04

/**
 * @brief PendSVを保留状態にするビット。
 */
#define ICSR_PENDSVSET (1U << 28)

/**
 * @brief PendSV例外を要求する。
 *
 * @details
 * ICSRレジスタのPendSVSETビットを設定し、
 * 割込み処理や現在の処理が終了した後に
 * コンテキスト切替を実行させる。
 */
static inline void dispatch(void)
{
    out_w(SCB_ICSR, ICSR_PENDSVSET);
}

/**
 * @brief 次に実行するタスクを選択するスケジューラ。
 */
extern void scheduler(void);

/**
 * @brief タスクの初期実行コンテキストを作成する。
 *
 * @param[in] sp   タスクスタック領域。
 * @param[in] ssize タスクスタックのサイズ。
 * @param[in] fp   タスク開始関数。
 *
 * @return void* 作成した初期コンテキストへのポインタ。
 */
extern void *make_context(UW *sp, UINT ssize, void (*fp)());

/**
 * @brief TCBをキューの末尾へ追加する。
 *
 * @param[in,out] queue TCBキューの先頭ポインタ。
 * @param[in] tcb 追加するTCB。
 */
extern void tqueue_add_entry(TCB **queue, TCB *tcb);

/**
 * @brief キューの先頭TCBを削除する。
 *
 * @param[in,out] queue TCBキューの先頭ポインタ。
 */
extern void tqueue_remove_top(TCB **queue);

/**
 * @brief 指定したTCBをキューから削除する。
 *
 * @param[in,out] queue TCBキューの先頭ポインタ。
 * @param[in] tcb 削除するTCB。
 */
extern void tqueue_remove_entry(TCB **queue, TCB *tcb);

/**
 * @brief カーネルオブジェクトの登録状態。
 */
typedef enum {
    KS_NONEXIST = 0, /**< 未登録。 */
    KS_EXIST    = 1  /**< 登録済み。 */
} KSSTAT;

/**
 * @brief イベントフラグ管理ブロック。
 */
typedef struct st_flgcb {
    KSSTAT state; /**< イベントフラグの登録状態。 */
    UINT flgptn;  /**< 現在設定されているイベントフラグ値。 */
} FLGCB;

/**
 * @brief セマフォ管理ブロック。
 */
typedef struct semaphore_control_block {
    KSSTAT state; /**< セマフォの登録状態。 */
    INT semcnt;   /**< 現在利用可能な資源数。 */
    INT maxsem;   /**< セマフォ資源数の最大値。 */
} SEMCB;

/**
 * @brief ミューテックス管理ブロック。
 */
typedef struct {
    KSSTAT state; /**< ミューテックスの登録状態。 */
    ATR attr;     /**< ミューテックス属性。 */
    TCB *owner;   /**< 所有タスク。NULLの場合は未ロック。 */
} MTXCB;

/**
 * @brief 所有タスクが保持するミューテックスをすべて解放する。
 *
 * @param[in] owner ミューテックスを保持しているタスク。
 *
 * @note 割込み禁止状態で呼び出すこと。
 *       呼び出し側で最後にscheduler()を実行する。
 */
extern void mutex_release_all(TCB *owner);

/**
 * @brief ミューテックス待ちタスクのタイムアウトを処理する。
 *
 * @param[in] waiter タイムアウトした待ちタスク。
 */
extern void mutex_wait_timeout(TCB *waiter);

/**
 * @brief 固定長メッセージキュー管理ブロック。
 */
typedef struct message_queue_control_block {
    KSSTAT state; /**< メッセージキューの登録状態。 */
    UB *buffer;   /**< メッセージ格納領域。 */
    INT msgsz;    /**< 1メッセージあたりのサイズ。 */
    INT maxmsg;   /**< 格納可能な最大メッセージ数。 */
    INT count;    /**< 現在格納されているメッセージ数。 */
    INT head;     /**< 次に読み出すメッセージの位置。 */
    INT tail;     /**< 次に書き込むメッセージの位置。 */
} MSGQCB;

/** TryKernelのOSメイン関数。 */
extern int main(void);

/** ユーザアプリケーションのメイン関数。 */
extern int usermain(void);

/**
 * @brief システムタイマ周期処理を実行する。
 *
 * @details
 * SysTick割込みから呼び出され、
 * 周期処理やタスクの待ち時間管理に使用される。
 */
extern void cyclic_tick(void);

#endif  /* KNLDEF_H */
