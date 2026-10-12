/**
 * @file    eventflag.c
 * @brief   TryKernelのイベントフラグ機能を実装する。
 *
 * @details
 * イベントフラグの生成、設定、クリア、および待ち合わせを管理する。
 *
 * イベントフラグは、複数のイベント状態をビットパターンで表現し、
 * タスク間または割込み処理とタスク間の通知に使用する。
 *
 * イベントフラグを待つタスクは、次の条件で待ち解除される。
 *
 * - TWF_ORW:
 *   待ち合わせるビットのいずれかが設定された場合。
 * - TWF_ANDW:
 *   待ち合わせるビットがすべて設定された場合。
 *
 * 待ち条件が成立した場合は、待ちタスクをウェイトキューから外して
 * READY状態へ移行し、必要に応じてスケジューラを実行する。
 *
 * TWF_BITCLRが指定されている場合は、待ち合わせに使用したビットを
 * クリアする。TWF_CLRが指定されている場合は、イベントフラグ全体を
 * クリアする。
 *
 * @note
 * tk_wai_flg()はタスクコンテキストから呼び出す必要があり、
 * 割込み・例外コンテキストからの待ちは禁止されている。
 *
 * @note
 * tk_set_flg()は、UART受信割込みなどから呼び出されることがある。
 * イベントフラグを設定した後、待ち状態のタスクをREADY状態へ移行する。
 */
#include <trykernel.h>
#include <knldef.h>
/**
 * @brief イベントフラグ管理ブロックの配列。
 *
 * 各イベントフラグの登録状態と、現在設定されている
 * ビットパターンを保持する。
 */
FLGCB flgcb_tbl[CNF_MAX_FLGID];

/**
 * @brief イベントフラグを生成する。
 *
 * @details
 * 未使用のイベントフラグ管理ブロックを検索して確保し、
 * 指定された初期ビットパターンを設定する。
 *
 * 生成されたイベントフラグには、管理テーブルの内部番号に1を加えた
 * IDを割り当てる。管理情報の検索および初期化を行う間は、
 * 割込みを禁止する。
 *
 * @param[in] pk_cflg イベントフラグ生成情報へのポインタ。
 *                    NULLは指定できない。
 *
 * @return ID 生成されたイベントフラグIDまたはエラーコード。
 * @retval 正の値 生成されたイベントフラグID。
 * @retval E_PAR pk_cflgがNULLである。
 * @retval E_LIMIT 生成可能なイベントフラグ数の上限に達している。
 */
ID tk_cre_flg(const T_CFLG *pk_cflg)
{
    ID      flgid;
    UINT    intsts;
    if(pk_cflg == NULL) return E_PAR;
    DI(intsts);     // 割込み禁止
    for(flgid = 0; flgid < CNF_MAX_FLGID &&  flgcb_tbl[flgid].state != KS_NONEXIST; flgid++);

    if(flgid < CNF_MAX_FLGID) {
        flgcb_tbl[flgid].state = KS_EXIST;
        flgcb_tbl[flgid].flgptn = pk_cflg->iflgptn;
        flgid++;
    } else {
        flgid = E_LIMIT;
    }
    EI(intsts);      // 割込み許可
    return flgid;
}
/**
 * @brief イベントフラグの待ち条件が成立しているか確認する。
 *
 * @details
 * 現在のイベントフラグ値と待ち合わせるビットパターンを比較し、
 * 指定された待ちモードに応じて条件成立を判定する。
 *
 * TWF_ORWが指定されている場合は、待ち合わせるビットのいずれか1つ
 * 以上が設定されていれば条件成立とする。
 *
 * TWF_ORWが指定されていない場合は、待ち合わせるビットがすべて
 * 設定されていれば条件成立とする。
 *
 * @param[in] flgptn 現在のイベントフラグ値。
 * @param[in] waiptn 待ち合わせるイベントフラグのビットパターン。
 * @param[in] wfmode イベントフラグの待ちモード。
 *
 * @retval TRUE  待ち条件が成立している。
 * @retval FALSE 待ち条件が成立していない。
 */
static BOOL check_flag(UINT flgptn, UINT waiptn, UINT wfmode)
{
    if(wfmode & TWF_ORW) {
        return ((flgptn & waiptn) != 0);
    } else {
        return ((flgptn &waiptn) == waiptn);
    }
}
/**
 * @brief イベントフラグを設定し、待ちタスクを解除する。
 *
 * @details
 * 指定されたイベントフラグ管理ブロックへsetptnのビットを設定する。
 *
 * フラグ設定後、イベントフラグを待っているタスクを検索し、
 * 待ち条件が成立したタスクをウェイトキューから外してREADY状態へ
 * 移行する。条件成立時には、現在のイベントフラグ値をタスクの
 * 取得先へ格納する。
 *
 * TWF_BITCLRが指定されている場合は、待ち合わせに使用したビットを
 * クリアする。TWF_CLRが指定されている場合は、イベントフラグ全体を
 * クリアする。
 *
 * READY状態へ移行したタスクが存在する場合は、待ちタスクの処理完了後に
 * scheduler()を1回実行する。
 *
 * @param[in] flgid   設定対象のイベントフラグID。
 * @param[in] setptn  設定するイベントフラグのビットパターン。
 *
 * @return ER 処理結果。
 * @retval E_OK    イベントフラグの設定に成功した。
 * @retval E_ID    イベントフラグIDが不正である。
 * @retval E_NOEXS 指定されたイベントフラグが存在しない。
 *
 * @note
 * イベントフラグ管理情報と待ちキューを更新する間は割込みを禁止する。
 * 割込み処理から呼び出す場合は、待ちを伴う処理を行わないこと。
 */
ER tk_set_flg( ID flgid, UINT setptn )
{
    FLGCB   *flgcb;
    TCB     *tcb, *next;
    ER      err = E_OK;
    UINT    intsts;
    BOOL    need_schedule = FALSE;

    if(flgid <= 0 || flgid > CNF_MAX_FLGID) return E_ID;

    DI(intsts);     // 割込み禁止
    flgcb = &flgcb_tbl[--flgid];
    if(flgcb->state == KS_EXIST) {
        flgcb->flgptn |= setptn;            // フラグのセット

        for( tcb = wait_queue; tcb != NULL; tcb = next) {
            next = tcb->next;  // tqueue_add_entry()で tcb->next= NULLとしているため
            if((tcb->waifct == TWFCT_FLG) && (tcb->waiobj == flgid)) {
                if(check_flag(flgcb->flgptn, tcb->waiptn, tcb->wfmode)) {   // 条件成立の確認
                    tqueue_remove_entry( &wait_queue, tcb);                 // タスクをウェイトキューから外す
                    tcb->state	= TS_READY;
                    tcb->waifct	= TWFCT_NON;
                    *tcb->p_flgptn = flgcb->flgptn;
                    tqueue_add_entry( &ready_queue[PRI_INDEX(tcb->itskpri)], tcb);     // タスクをレディキューへつなぐ

                    /* READYタスクが発生したことを記憶 */
                    need_schedule = TRUE;

                    if ((tcb->wfmode & TWF_BITCLR) != 0 ) {
                        if ( (flgcb->flgptn &= ~(tcb->waiptn)) == 0 ) {     // 対象フラグのクリア
                            break;
                        }
                    }
                    if ((tcb->wfmode & TWF_CLR) != 0 ) {
                        flgcb->flgptn = 0;                                  // 全フラグのクリア
                        break;
                    }
                }
            }
        }
        if(need_schedule) {
            /* 待ちタスクの処理完了後、スケジューラを１回だけ実行 */
            scheduler();    // スケジューラを実行
        }
    } else {
        err = E_NOEXS;
    }

    EI(intsts);     // 割込み許可
    return err;
}
/**
 * @brief イベントフラグをクリアする。
 *
 * @details
 * 指定されたイベントフラグ管理ブロックのフラグ値を更新し、
 * clrptnで指定されたビットを対象にクリア処理を行う。
 *
 * イベントフラグ管理情報を更新する間は割込みを禁止する。
 *
 * @param[in] flgid  クリア対象のイベントフラグID。
 * @param[in] clrptn クリア対象のイベントフラグのビットパターン。
 *
 * @return ER 処理結果。
 * @retval E_OK    イベントフラグのクリアに成功した。
 * @retval E_ID    イベントフラグIDが不正である。
 * @retval E_NOEXS 指定されたイベントフラグが存在しない。
 *
 * @note
 * 一般的に、指定ビットをクリアする処理は
 * `flgptn &= ~clrptn`で実装する。
 * 現在の実装が`flgptn &= clrptn`の場合は、
 * clrptnに含まれないビットをクリアする動作になるため、
 * 意図した仕様と一致しているか確認が必要である。
 */
ER tk_clr_flg( ID flgid, UINT clrptn )
{
    FLGCB   *flgcb;
    ER      err = E_OK;
    UINT    intsts;

    if(flgid <= 0 || flgid > CNF_MAX_FLGID) return E_ID;

    DI(intsts);     // 割込み禁止
    flgcb = &flgcb_tbl[--flgid];
    if(flgcb->state == KS_EXIST) {
        flgcb->flgptn &= clrptn;        // フラグのクリア
    } else {
        err = E_NOEXS;
    }
    EI(intsts);     // 割込み許可
    return err;
}
/**
 * @brief イベントフラグが指定条件を満たすまで待つ。
 *
 * @details
 * 指定されたイベントフラグのビットパターンを確認し、
 * 待ち条件が成立している場合は直ちに処理を完了する。
 *
 * 待ち条件が成立した場合、成立時点のイベントフラグ値を
 * p_flgptnへ格納する。TWF_BITCLRが指定されている場合は
 * 待ち合わせに使用したビットをクリアし、TWF_CLRが指定されている
 * 場合はイベントフラグ全体をクリアする。
 *
 * 待ち条件が成立しておらず、tmoutがTMO_POLの場合は待機せずに
 * E_TMOUTを返す。それ以外の場合は、現在のタスクをWAIT状態へ移行し、
 * イベントフラグの待ちキューへ登録してスケジューラを実行する。
 *
 * @param[in]  flgid    待ち対象のイベントフラグID。
 * @param[in]  waiptn   待ち合わせるイベントフラグのビットパターン。
 * @param[in]  wfmode   待ちモード。
 *                      TWF_ORWまたはAND待ち、および
 *                      TWF_BITCLR、TWF_CLRを指定する。
 * @param[out] p_flgptn 待ち解除時のイベントフラグ値の格納先。
 * @param[in]  tmout    待ち時間。
 *                      TMO_POLは待機なし、
 *                      TMO_FEVRは無期限待機を示す。
 *
 * @return ER 処理結果。
 * @retval E_OK    待ち条件が成立した。
 * @retval E_CTX   割込みまたは例外コンテキストから呼び出された。
 * @retval E_ID    イベントフラグIDが不正である。
 * @retval E_PAR   p_flgptnがNULL、またはwaiptnが0である。
 * @retval E_NOEXS 指定されたイベントフラグが存在しない。
 * @retval E_TMOUT 待ち条件が成立せず、待ち時間が満了した。
 *
 * @note
 * このAPIはタスクコンテキストから呼び出す必要がある。
 * 待ち状態への移行およびイベントフラグ管理情報の更新中は、
 * 割込みを禁止する。
 */
ER tk_wai_flg(
    ID flgid,
    UINT waiptn,
    UINT wfmode,
    UINT *p_flgptn,
    TMO tmout
){
    FLGCB   *flgcb;
    ER      err = E_OK;
    UINT    intsts;

    /* 割り込み・例外コンテキストからの待ちは禁止 */
    if(is_interrupt_context()) return E_CTX;

    if(flgid <= 0 || flgid > CNF_MAX_FLGID) return E_ID;
    if(p_flgptn == NULL || waiptn == 0) return E_PAR;

    DI(intsts);     // 割込み禁止
    flgcb = &flgcb_tbl[--flgid];
    if(flgcb->state == KS_EXIST) {
        if(check_flag(flgcb->flgptn, waiptn, wfmode)) {     // 待ち条件が成立している場合
            *p_flgptn = flgcb->flgptn;                      // 条件成立時のフラグ値を返す
            if ( (wfmode & TWF_BITCLR) != 0 ) {
                flgcb->flgptn &= ~waiptn;                   // 該当フラグのクリア
            }
            if ( (wfmode & TWF_CLR) != 0 ) {
                flgcb->flgptn = 0;                          // 全フラグのクリア
            }
        } else if(tmout == TMO_POL) {                       // 待ち条件不成立、かつ、待ち時間0の場合
            err = E_TMOUT;
        } else {                                            // 待ち条件不成立、待ち状態に移行
            tqueue_remove_top(&ready_queue[PRI_INDEX(cur_task->itskpri)]);     // タスクをレディキューから外す

            /* TCBの各種情報を変更する */
            cur_task->state     = TS_WAIT;      // タスクの状態を待ち状態に変更
            cur_task->waifct    = TWFCT_FLG;    // 待ち要因を設定
            cur_task->waiobj    = flgid;        // 待ちイベントフラグIDを設定
            cur_task->waitim    = ((tmout == TMO_FEVR)? tmout: tmout + TIMER_PERIOD);    // 待ち時間を設定
            cur_task->waiptn    = waiptn;
            cur_task->wfmode    = wfmode;
            cur_task->p_flgptn  = p_flgptn;
            cur_task->waierr    = &err;

            tqueue_add_entry(&wait_queue, cur_task);                // タスクをウェイトキューに繋ぐ
            scheduler();                                            // スケジューラを実行
        }
    } else {
        err = E_NOEXS;      // 未登録のイベントフラグ
    }

    EI(intsts);     // 割込み許可
    return err;
}
