/**
 * @file    cyclic.c
 * @brief   周期ハンドラの生成・制御・実行を管理する。
 *
 * @details
 * TryKernelの周期ハンドラ機能を実装する。
 *
 * 周期ハンドラの生成、開始、停止、削除、状態参照を行い、
 * 各周期ハンドラの残り時間を管理する。
 *
 * SysTick割込みからcyclic_tick()を呼び出すことで、
 * 周期が満了した周期ハンドラを実行する。
 * 周期ハンドラの管理情報はCYCCB配列で保持する。
 *
 * 周期ハンドラの管理操作はタスクコンテキストから行い、
 * SysTickコンテキストでは周期満了時のコールバック実行のみを行う。
 *
 * @note
 * 周期ハンドラのコールバック関数はSysTick割込みコンテキストで
 * 実行される。そのため、コールバック内で待ち状態へ移行するAPIや
 * 時間のかかる処理を実行してはならない。
 *
 * @note
 * 周期がシステムタイマ周期の整数倍でない場合でも、
 * 設定された周期を基準に次回実行時刻を計算する。
 */
#include <trykernel.h>
#include <knldef.h>

/**
 * @brief 周期ハンドラ管理ブロック。
 *
 * @details
 * 1つの周期ハンドラに関する登録状態、実行状態、
 * 生成時の設定、次回実行までの残り時間を保持する。
 */
typedef struct {
    /** 周期ハンドラが生成済みかを示す。 */
    BOOL exists;

    /** 周期ハンドラが実行中かを示す。 */
    BOOL active;

    /** 周期ハンドラの生成時設定。 */
    T_CCYC config;

    /** 次回実行までの残り時間。 */
    RELTIM remaining;
} CYCCB;

/**
 * @brief 周期ハンドラ管理ブロックの配列。
 *
 * 周期ハンドラIDに対応する管理情報を保持する。
 */
static CYCCB cyclic[CNF_MAX_CYCID];

/**
 * @brief 周期ハンドラを生成する。
 *
 * @details
 * 指定された周期ハンドラ生成情報を検証し、未使用の周期ハンドラ管理
 * ブロックを確保して初期化する。
 *
 * 周期、初回位相、コールバック関数、拡張情報を管理ブロックへ保存する。
 * TA_STA属性が指定されている場合は、生成直後から周期ハンドラを
 * 実行可能状態にする。
 *
 * 管理ブロックの検索と初期化を行う間は割込みを禁止する。
 *
 * @param[in] pk_ccyc 周期ハンドラ生成情報へのポインタ。
 *
 * @return ID 生成された周期ハンドラIDまたはエラーコード。
 * @retval 正の値 生成された周期ハンドラID。
 * @retval E_CTX 割込みまたは例外コンテキストから呼び出された。
 * @retval E_PAR pk_ccycがNULL、コールバック関数がNULL、
 *               周期または初回位相が不正である。
 * @retval E_RSATR 未対応の周期ハンドラ属性が指定された。
 * @retval E_LIMIT 生成可能な周期ハンドラ数の上限に達している。
 */
ID tk_cre_cyc(const T_CCYC *pk_ccyc)
{
    UINT intsts;
    ID i;
    if(is_interrupt_context()) return E_CTX;
    if(pk_ccyc == NULL || pk_ccyc->cychdr == NULL ||
       pk_ccyc->cyctim <= 0 || pk_ccyc->cycphs < 0) return E_PAR;
    if((pk_ccyc->cycatr & ~(TA_HLNG | TA_STA)) != 0) return E_RSATR;
    DI(intsts);
    for(i = 0; i < CNF_MAX_CYCID; i++) {
        if(!cyclic[i].exists) {
            cyclic[i].config = *pk_ccyc;
            cyclic[i].remaining = pk_ccyc->cycphs;
            cyclic[i].active = (pk_ccyc->cycatr & TA_STA) != 0;
            cyclic[i].exists = TRUE;
            EI(intsts);
            return i + 1;
        }
    }
    EI(intsts);
    return E_LIMIT;
}

/**
 * @brief 周期ハンドラの開始、停止、削除、状態参照を内部処理する。
 *
 * @details
 * operationの値に応じて、指定された周期ハンドラ管理ブロックを操作する。
 *
 * - 0: 周期ハンドラを開始する。
 * - 1: 周期ハンドラを停止する。
 * - 2: 周期ハンドラを停止して削除する。
 * - 3: 周期ハンドラの状態と残り時間を参照する。
 *
 * 周期ハンドラ管理情報へのアクセスは、SysTickによる同時更新を防ぐため
 * 割込み禁止状態で行う。状態参照の場合は、statusへ現在の実行状態と
 * 次回実行までの残り時間を書き込む。
 *
 * @param[in]  id        操作対象の周期ハンドラID。
 * @param[in]  operation 実行する操作。
 *                       0は開始、1は停止、2は削除、3は状態参照。
 * @param[out] status    状態参照時の結果格納先。
 *                       operationが3の場合に必要。それ以外ではNULLを指定する。
 *
 * @return ER 処理結果。
 * @retval E_OK    操作に成功した。
 * @retval E_CTX   割込みまたは例外コンテキストから呼び出された。
 * @retval E_ID    周期ハンドラIDが不正である。
 * @retval E_PAR   状態参照時にstatusがNULLである。
 * @retval E_NOEXS 指定された周期ハンドラが存在しない。
 *
 * @note
 * この関数は周期ハンドラAPIから呼び出される内部関数である。
 */
static ER control(ID id, INT operation, T_RCYC *status)
{
    UINT intsts;
    CYCCB *cb;
    if(is_interrupt_context()) return E_CTX;
    if(id < 1 || id > CNF_MAX_CYCID) return E_ID;
    if(operation == 3 && status == NULL) return E_PAR;
    DI(intsts);
    cb = &cyclic[id - 1];
    if(!cb->exists) {
        EI(intsts);
        return E_NOEXS;
    }
    switch(operation) {
    case 0:
        cb->remaining = cb->config.cycphs;
        cb->active = TRUE;
        break;
    case 1: cb->active = FALSE; break;
    case 2: cb->active = FALSE; cb->exists = FALSE; break;
    case 3:
        status->cycstat = cb->active ? TCYC_STA : TCYC_STP;
        status->lfttim = cb->active ? cb->remaining : 0;
        break;
    }
    EI(intsts);
    return E_OK;
}
/**
 * @brief 周期ハンドラを開始する。
 *
 * @details
 * 指定された周期ハンドラを開始状態にし、
 * 初回実行までの残り時間を初回位相に設定する。
 *
 * @param[in] id 開始する周期ハンドラID。
 *
 * @return ER 処理結果。
 * @retval E_OK 周期ハンドラの開始に成功した。
 * @retval E_CTX 割込みまたは例外コンテキストから呼び出された。
 * @retval E_ID 周期ハンドラIDが不正である。
 * @retval E_NOEXS 指定された周期ハンドラが存在しない。
 */
ER tk_sta_cyc(ID id)
{
    return control(id, 0, NULL);
}

/**
 * @brief 周期ハンドラを停止する。
 *
 * @details
 * 指定された周期ハンドラを停止状態にする。
 * 周期ハンドラの管理情報は保持されるため、停止後に再開できる。
 *
 * @param[in] id 停止する周期ハンドラID。
 *
 * @return ER 処理結果。
 * @retval E_OK 周期ハンドラの停止に成功した。
 * @retval E_CTX 割込みまたは例外コンテキストから呼び出された。
 * @retval E_ID 周期ハンドラIDが不正である。
 * @retval E_NOEXS 指定された周期ハンドラが存在しない。
 */
ER tk_stp_cyc(ID id)
{
    return control(id, 1, NULL);
}

/**
 * @brief 周期ハンドラを削除する。
 *
 * @details
 * 指定された周期ハンドラを停止し、管理情報を未使用状態に戻す。
 *
 * @param[in] id 削除する周期ハンドラID。
 *
 * @return ER 処理結果。
 * @retval E_OK 周期ハンドラの削除に成功した。
 * @retval E_CTX 割込みまたは例外コンテキストから呼び出された。
 * @retval E_ID 周期ハンドラIDが不正である。
 * @retval E_NOEXS 指定された周期ハンドラが存在しない。
 */
ER tk_del_cyc(ID id)
{
    return control(id, 2, NULL);
}

/**
 * @brief 周期ハンドラの状態を参照する。
 *
 * @details
 * 指定された周期ハンドラの実行状態と、
 * 次回実行までの残り時間をstatusへ格納する。
 *
 * @param[in]  id     参照する周期ハンドラID。
 * @param[out] status 周期ハンドラの状態と残り時間の格納先。
 *
 * @return ER 処理結果。
 * @retval E_OK 周期ハンドラ状態の取得に成功した。
 * @retval E_CTX 割込みまたは例外コンテキストから呼び出された。
 * @retval E_ID 周期ハンドラIDが不正である。
 * @retval E_PAR statusがNULLである。
 * @retval E_NOEXS 指定された周期ハンドラが存在しない。
 */
ER tk_ref_cyc(ID id, T_RCYC *status)
{
    return control(id, 3, status);
}
/**
 * @brief 周期ハンドラの経過時間を更新し、満了したハンドラを実行する。
 *
 * @details
 * SysTick割込みから呼び出され、登録されているすべての周期ハンドラを
 * 確認する。
 *
 * 実行中の周期ハンドラについて残り時間を減算し、周期が満了した場合は
 * 登録されたコールバック関数を呼び出す。その後、次回実行までの残り時間を
 * 更新する。
 *
 * システムタイマ周期で割り切れない周期にも対応するため、
 * 前回の実行時刻からの超過時間を考慮して次回実行時刻を計算する。
 * 1回のSysTick処理で、各周期ハンドラのコールバックは最大1回だけ実行する。
 *
 * 周期ハンドラ管理情報を更新する間は割込みを禁止する。
 *
 * @note
 * コールバック関数はSysTick割込みコンテキストで実行される。
 * コールバック内で待ち状態へ移行するAPIや、時間のかかる処理を
 * 実行してはならない。
 */
void cyclic_tick(void)
{
    UINT intsts;
    ID i;
    DI(intsts);
    for(i = 0; i < CNF_MAX_CYCID; i++) {
        CYCCB *cb = &cyclic[i];
        if(!cb->exists || !cb->active) continue;
        if(cb->remaining > TIMER_PERIOD) {
            cb->remaining -= TIMER_PERIOD;
            continue;
        }
        /* Keep the nominal phase for periods not divisible by the tick.
         * Coalesce sub-tick expirations into at most one callback per tick.
         * No addition of two potentially large signed times is needed. */
        RELTIM overdue = TIMER_PERIOD - cb->remaining;
        cb->remaining = cb->config.cyctim - overdue % cb->config.cyctim;
        cb->config.cychdr(cb->config.exinf);
    }
    EI(intsts);
}
