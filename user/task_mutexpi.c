/**
 * @file    task_mutexpi.c
 * @brief   優先度継承ミューテックスの動作を3タスクで検証する。
 *
 * @details
 * 低優先度タスクがミューテックスを保持中に高優先度タスクがロックを待ち、
 * 低優先度タスクの実行優先度が継承されることを確認する。中優先度タスクも
 * 実行可能にした状態で、優先度継承によって実行順が保護されることを検証し、
 * 優先度とイベント順序をUARTへ表示する。
 */

#include <trykernel.h>
#include <knldef.h>
#include "task_mutexpi.h"
#include "uart_tx.h"

#define MUTEXPI_PRIORITY_HIGH       2       //!< 高優先度タスクの基本優先度。
#define MUTEXPI_PRIORITY_MEDIUM     8       //!< 中優先度タスクの基本優先度。
#define MUTEXPI_PRIORITY_LOW       14       //!< 低優先度タスクの基本優先度。
#define MUTEXPI_STACK_SIZE       1024       //!< 各試験タスクのスタックサイズ（バイト）。
#define MUTEXPI_TIMEOUT           1000      //!< 試験イベント待ちのタイムアウト（ミリ秒）。
#define MUTEXPI_BUSY_LOOPS      500000U     //!< 低優先度タスクがロック保持中に実行する待ちループ回数。

#define MUTEXPI_LOW_LOCKED       (1U << 0) //!< LOWがミューテックスを取得したことを示すイベント。
#define MUTEXPI_LOW_DONE         (1U << 1) //!< LOWタスクの試験処理完了イベント。
#define MUTEXPI_HIGH_DONE        (1U << 2) //!< HIGHタスクの試験処理完了イベント。
#define MUTEXPI_MEDIUM_DONE      (1U << 3) //!< MEDIUMタスクの試験処理完了イベント。
#define MUTEXPI_ALL_DONE         \
    (MUTEXPI_LOW_DONE | MUTEXPI_HIGH_DONE | MUTEXPI_MEDIUM_DONE) //!< 全タスクの完了を示すイベントビット集合。

static UW stack_high[MUTEXPI_STACK_SIZE / sizeof(UW)]; //!< HIGHタスクのスタック領域。
static UW stack_medium[MUTEXPI_STACK_SIZE / sizeof(UW)]; //!< MEDIUMタスクのスタック領域。
static UW stack_low[MUTEXPI_STACK_SIZE / sizeof(UW)]; //!< LOWタスクのスタック領域。
static ID task_high_id;   //!< 高優先度試験タスクのID。
static ID task_medium_id; //!< 中優先度試験タスクのID。
static ID task_low_id;    //!< 低優先度試験タスクのID。
static ID test_mutex_id;  //!< 試験で共有する優先度継承ミューテックスのID。
static ID test_flag_id;   //!< タスク間の進行・完了通知に使用するイベントフラグID。
static BOOL test_running; //!< 試験が実行中の場合にTRUE。

static volatile UW event_sequence;          //!< 試験イベントの発生順を記録する通番。
static volatile UW low_lock_order;          //!< LOWがミューテックスを取得した順番。
static volatile UW low_unlock_order;        //!< LOWがミューテックスを解放した順番。
static volatile UW high_lock_order;         //!< HIGHがミューテックスを取得した順番。
static volatile UW medium_run_order;        //!< MEDIUMが実行された順番。
static volatile PRI low_base_before;        //!< LOWの基本優先度。
static volatile PRI low_current_before;     //!< HIGHがロック待ちする前のLOWの実行優先度。
static volatile PRI low_current_inherited;  //!< HIGHのロック待ち中に記録したLOWの実行優先度。
static volatile BOOL medium_ran;            //!< MEDIUMタスクが実行された場合にTRUE。
static volatile BOOL medium_ran_during_lock; //!< LOWのロック保持中にMEDIUMが実行された場合にTRUE。

static void task_mutexpi_high(INT stacd, void *exinf);
static void task_mutexpi_medium(INT stacd, void *exinf);
static void task_mutexpi_low(INT stacd, void *exinf);

/** HIGHタスクを生成するためのカーネル設定。 */
static T_CTSK task_high_config = {
    .tskatr = TA_HLNG | TA_RNG3 | TA_USERBUF,
    .task = task_mutexpi_high,
    .itskpri = MUTEXPI_PRIORITY_HIGH,
    .stksz = MUTEXPI_STACK_SIZE,
    .bufptr = stack_high,
};

/** MEDIUMタスクを生成するためのカーネル設定。 */
static T_CTSK task_medium_config = {
    .tskatr = TA_HLNG | TA_RNG3 | TA_USERBUF,
    .task = task_mutexpi_medium,
    .itskpri = MUTEXPI_PRIORITY_MEDIUM,
    .stksz = MUTEXPI_STACK_SIZE,
    .bufptr = stack_medium,
};

/** LOWタスクを生成するためのカーネル設定。 */
static T_CTSK task_low_config = {
    .tskatr = TA_HLNG | TA_RNG3 | TA_USERBUF,
    .task = task_mutexpi_low,
    .itskpri = MUTEXPI_PRIORITY_LOW,
    .stksz = MUTEXPI_STACK_SIZE,
    .bufptr = stack_low,
};

/**
 * @brief ミューテックスを先に取得し、優先度継承中のLOWタスクの動作を記録する。
 * @param[in] stacd タスク起動コード（この関数では使用しない）。
 * @param[in] exinf タスク拡張情報（この関数では使用しない）。
 * @details ミューテックス取得後に試験側へ通知し、HIGHのロック待ち中に
 *          LOWの実行優先度とMEDIUMの実行有無を記録してからロックを解放する。
 */
static void task_mutexpi_low(INT stacd, void *exinf)
{
    volatile UW i;
    (void)stacd;
    (void)exinf;

    if(tk_loc_mtx(test_mutex_id, TMO_FEVR) != E_OK) {
        (void)tk_set_flg(test_flag_id, MUTEXPI_LOW_DONE);
        tk_ext_tsk();
        return;
    }

    low_base_before = cur_task->btskpri;
    low_current_before = cur_task->itskpri;
    low_lock_order = ++event_sequence;

    /* Wake the command task so it can start medium and high tasks. */
    (void)tk_set_flg(test_flag_id, MUTEXPI_LOW_LOCKED);

    /* The high task now waits for this mutex, so LOW must inherit priority 2. */
    low_current_inherited = cur_task->itskpri;
    for(i = 0U; i < MUTEXPI_BUSY_LOOPS; i++) {
        __asm__ volatile("nop");
    }
    medium_ran_during_lock = medium_ran;
    low_unlock_order = ++event_sequence;
    (void)tk_unl_mtx(test_mutex_id);
    (void)tk_set_flg(test_flag_id, MUTEXPI_LOW_DONE);
    tk_ext_tsk();
}

/**
 * @brief ミューテックス取得を待ち、取得後に解放するHIGHタスク。
 * @param[in] stacd タスク起動コード（この関数では使用しない）。
 * @param[in] exinf タスク拡張情報（この関数では使用しない）。
 * @details ロック取得順を記録し、処理完了イベントを通知して終了する。
 */
static void task_mutexpi_high(INT stacd, void *exinf)
{
    (void)stacd;
    (void)exinf;

    if(tk_loc_mtx(test_mutex_id, TMO_FEVR) == E_OK) {
        high_lock_order = ++event_sequence;
        (void)tk_unl_mtx(test_mutex_id);
    }
    (void)tk_set_flg(test_flag_id, MUTEXPI_HIGH_DONE);
    tk_ext_tsk();
}

/**
 * @brief 実行順を記録して完了を通知するMEDIUMタスク。
 * @param[in] stacd タスク起動コード（この関数では使用しない）。
 * @param[in] exinf タスク拡張情報（この関数では使用しない）。
 */
static void task_mutexpi_medium(INT stacd, void *exinf)
{
    (void)stacd;
    (void)exinf;
    medium_ran = TRUE;
    medium_run_order = ++event_sequence;
    (void)tk_set_flg(test_flag_id, MUTEXPI_MEDIUM_DONE);
    tk_ext_tsk();
}

/**
 * @brief 優先度継承ミューテックス、イベントフラグ、試験タスクを作成する。
 * @retval E_OK すべてのオブジェクトとタスクを作成できた。
 * @return オブジェクトまたはタスク作成に失敗した場合はTRYカーネルのエラーコード。
 */
ER task_mutexpi_init(void)
{
    T_CMTX cmtx = { .mtxatr = TA_INHERIT };
    T_CFLG cflg = { .flgatr = TA_TFIFO, .iflgptn = 0U };

    test_mutex_id = tk_cre_mtx(&cmtx);
    if(test_mutex_id < E_OK) return (ER)test_mutex_id;
    test_flag_id = tk_cre_flg(&cflg);
    if(test_flag_id < E_OK) return (ER)test_flag_id;

    task_high_id = tk_cre_tsk(&task_high_config);
    if(task_high_id < E_OK) return (ER)task_high_id;
    task_medium_id = tk_cre_tsk(&task_medium_config);
    if(task_medium_id < E_OK) return (ER)task_medium_id;
    task_low_id = tk_cre_tsk(&task_low_config);
    if(task_low_id < E_OK) return (ER)task_low_id;
    return E_OK;
}
/**
 * @brief 3タスクを使ってミューテックス優先度継承を検証し、結果を表示する。
 * @details LOWがロックを保持している間にHIGHを待たせ、MEDIUMも起動する。
 *          LOWの実行優先度がHIGHの優先度へ継承され、LOWの解放後にHIGH、
 *          最後にMEDIUMが実行されることを優先度・順序の記録から判定する。
 *          すでに試験中の場合は新たな試験を開始しない。
 */
void task_mutexpi_run_test(void)
{
    UINT flag_pattern;
    ER err;
    BOOL passed;

    if(test_running != FALSE) {
        uart_tx_send("Mutex priority inheritance test is already running\r\n");
        return;
    }
    test_running = TRUE;
    event_sequence = 0U;
    low_lock_order = 0U;
    low_unlock_order = 0U;
    high_lock_order = 0U;
    medium_run_order = 0U;
    low_base_before = 0;
    low_current_before = 0;
    low_current_inherited = 0;
    medium_ran = FALSE;
    medium_ran_during_lock = FALSE;
    (void)tk_clr_flg(test_flag_id, 0U);

    uart_tx_send("Mutex priority inheritance test start\r\n");
    err = tk_sta_tsk(task_low_id, 0);
    if(err != E_OK) goto error;
    err = tk_wai_flg(test_flag_id, MUTEXPI_LOW_LOCKED,
                     TWF_ORW | TWF_BITCLR, &flag_pattern, MUTEXPI_TIMEOUT);
    if(err != E_OK) goto error;

    err = tk_sta_tsk(task_medium_id, 0);
    if(err != E_OK) goto error;
    err = tk_sta_tsk(task_high_id, 0);
    if(err != E_OK) goto error;

    err = tk_wai_flg(test_flag_id, MUTEXPI_ALL_DONE,
                     TWF_ANDW | TWF_CLR, &flag_pattern, MUTEXPI_TIMEOUT);
    if(err != E_OK) goto error;

    uart_tx_printf("LOW priority: base=%d before=%d inherited=%d\r\n",
                   low_base_before, low_current_before, low_current_inherited);
    uart_tx_printf("Execution order: low-lock=%u low-unlock=%u high-lock=%u medium=%u\r\n",
                   (UINT)low_lock_order, (UINT)low_unlock_order,
                   (UINT)high_lock_order, (UINT)medium_run_order);
    uart_tx_printf("Medium ran while LOW held mutex: %s\r\n",
                   medium_ran_during_lock != FALSE ? "YES" : "NO");

    passed = (low_base_before == MUTEXPI_PRIORITY_LOW) &&
             (low_current_before == MUTEXPI_PRIORITY_LOW) &&
             (low_current_inherited == MUTEXPI_PRIORITY_HIGH) &&
             (medium_ran_during_lock == FALSE) &&
             (low_lock_order < low_unlock_order) &&
             (low_unlock_order < high_lock_order) &&
             (high_lock_order < medium_run_order);
    uart_tx_printf("Mutex priority inheritance test: %s\r\n",
                   passed != FALSE ? "PASS" : "FAIL");
    test_running = FALSE;
    return;

error:
    uart_tx_printf("Mutex priority inheritance test error: %d\r\n", err);
    test_running = FALSE;
}
