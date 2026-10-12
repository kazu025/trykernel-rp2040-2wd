/**
 * @file    task_mange.c
 * @brief   タスクの生成、起動、終了を管理する。
 *
 * @details
 * TryKernelのタスク管理機能を実装する。
 *
 * タスク生成時には、タスク属性、開始関数、優先度、
 * スタック領域およびスタックサイズを検証し、
 * 未使用のタスク管理ブロック（TCB）を初期化する。
 *
 * タスク起動時には、タスクをDORMANT状態からREADY状態へ移行し、
 * 初期実行コンテキストを作成して優先度に対応するレディキューへ
 * 登録する。その後、スケジューラを実行する。
 *
 * タスク終了時には、タスクが所有しているミューテックスを解放し、
 * 待ちタスクへ所有権を移譲する。終了したタスクをレディキューから
 * 削除してDORMANT状態へ戻し、次に実行するタスクを選択する。
 *
 * タスク管理テーブルの更新とタスクキューの操作を行う間は、
 * 割込みを禁止する。
 *
 * @note
 * タスクIDは、タスク管理テーブルの内部インデックスに1を加えた値を
 * 使用する。
 */

#include <trykernel.h>
#include <knldef.h>
/**
 * @brief タスク管理ブロック（TCB）の配列。
 *
 * 各タスクの状態、実行コンテキスト、優先度、
 * スタック情報および待ち状態を保持する。
 */
TCB tcb_tbl[CNF_MAX_TSKID];

/**
 * @brief タスクを生成する。
 *
 * @details
 * 指定されたタスク生成情報を検証し、未使用のTCBを確保して初期化する。
 *
 * タスク開始関数、ユーザ指定スタック、タスク属性、初期優先度、
 * スタックサイズを設定し、生成されたタスクをDORMANT状態にする。
 *
 * この関数ではタスクを起動せず、タスクを実行可能状態へ移行する処理は
 * tk_sta_tsk()で行う。
 *
 * @param[in] pk_ctsk タスク生成情報へのポインタ。
 *                    NULLは指定できない。
 *
 * @return ID 生成されたタスクIDまたはエラーコード。
 * @retval 正の値 生成されたタスクID。
 * @retval E_PAR タスク生成情報、開始関数、スタック領域、
 *               優先度、またはスタックサイズが不正である。
 * @retval E_RSATR 未対応のタスク属性が指定された。
 * @retval E_LIMIT 生成可能なタスク数の上限に達している。
 *
 * @note
 * タスク管理テーブルを更新する間は割込みを禁止する。
 * タスクIDはTCB配列のインデックスに1を加えた値を返す。
 */
ID tk_cre_tsk( const T_CTSK *pk_ctsk )
{
    UINT    intsts;
    ID      tskid;
    INT     i;

    /* 引数チェック */
    if(pk_ctsk == NULL) return E_PAR;
    if(pk_ctsk->task == NULL|| pk_ctsk->bufptr == NULL) return E_PAR;
    if((pk_ctsk->tskatr & ~TA_RNG3) != (TA_HLNG|TA_USERBUF)) return E_RSATR;
    if(pk_ctsk->itskpri <= 0 || pk_ctsk->itskpri > CNF_MAX_TSKPRI) return E_PAR;
    if(pk_ctsk->stksz <= 0) return E_PAR;

    DI(intsts);     // 割込み禁止

    for(i = 0; i < CNF_MAX_TSKID; i++) {    // 未使用のTCBを検索
        if(tcb_tbl[i].state == TS_NONEXIST) break;
    }
    /* TCBの初期化 */
    if(i < CNF_MAX_TSKID) {
        tcb_tbl[i].state    = TS_DORMANT;
        tcb_tbl[i].pre      = NULL;
        tcb_tbl[i].next     = NULL;

        tcb_tbl[i].tskadr   = pk_ctsk->task;
        tcb_tbl[i].btskpri  = pk_ctsk->itskpri;
        tcb_tbl[i].itskpri  = pk_ctsk->itskpri;
        tcb_tbl[i].stksz    = pk_ctsk->stksz;
        tcb_tbl[i].stkadr   = pk_ctsk->bufptr;

        tskid = i+1;
    } else {
        tskid = (ID)E_LIMIT;    // タスクが既に最大数
    }

    EI(intsts);      // 割込み許可
    return tskid;
}
/**
 * @brief 生成済みのタスクを起動する。
 *
 * @details
 * 指定されたタスクがDORMANT状態の場合、そのタスクをREADY状態へ移行し、
 * タスクスタック上に初期実行コンテキストを作成する。
 *
 * 初期コンテキストをTCBへ保存した後、タスクを現在の実行優先度に対応する
 * レディキューへ登録し、スケジューラを実行する。
 *
 * DORMANT状態以外のタスクを起動することはできない。
 *
 * @param[in] tskid 起動するタスクID。
 * @param[in] stacd タスク起動コード。
 *                  現在の実装では使用しない。
 *
 * @return ER 処理結果。
 * @retval E_OK  タスクの起動に成功した。
 * @retval E_ID  タスクIDが不正である。
 * @retval E_OBJ タスクがDORMANT状態ではない。
 *
 * @note
 * タスク状態とレディキューを更新する間は割込みを禁止する。
 */
ER tk_sta_tsk(ID tskid, INT stacd)
{
    TCB     *tcb;
    UINT    intsts;
    ER      err = E_OK;
    (void)stacd;
    /* 引数チェック */
    if(tskid <= 0 || tskid > CNF_MAX_TSKID) return E_ID;
    DI(intsts);     // 割込み禁止

    tcb = &tcb_tbl[tskid-1];
    if(tcb->state == TS_DORMANT) {  // タスクを実行できる状態に変更
        tcb->state = TS_READY;
        tcb->context = make_context(tcb->stkadr, tcb->stksz, tcb->tskadr);
        tqueue_add_entry(&ready_queue[PRI_INDEX(tcb->itskpri)], tcb);
        scheduler();                // スケジューラを実行
    } else {
        err = E_OBJ;  // タスクを実行できない（休止状態ではない）
    }

    EI(intsts);     // 割込み許可
    return err;
}
/**
 * @brief 現在実行中のタスクを終了する。
 *
 * @details
 * 現在のタスクが所有しているミューテックスをすべて解放し、
 * 待ちタスクが存在する場合は所有権を移譲する。
 *
 * その後、現在のタスクをレディキューから削除してDORMANT状態へ移行し、
 * スケジューラを実行して別のREADYタスクへ処理を切り替える。
 *
 * @note
 * 通常、この関数はタスクを終了させた後に呼び出し元へ戻らない。
 * タスク管理情報とキューを更新する間は割込みを禁止する。
 */
void tk_ext_tsk(void)
{
    UINT	intsts;

    DI(intsts);     // 割込み禁止

    mutex_release_all(cur_task);    // 所有ロックを返し、待ちタスクへ引き継ぐ
    tqueue_remove_entry(&ready_queue[PRI_INDEX(cur_task->itskpri)], cur_task);
    cur_task->state = TS_DORMANT;    // タスクを休止状態へ

    scheduler();                    // スケジューラを実行
    EI(intsts);     // 割込み許可
}
