/**
 * @file    scheduler.c
 * @brief   READY状態のタスクから次に実行するタスクを選択する。
 *
 * @details
 * 優先度ごとのレディキューを先頭から検索し、最も高い優先度のタスクを
 * 次の実行タスクとして選択する。選択結果が現在実行中のタスクと異なる場合は、
 * ディスパッチャへコンテキスト切替を要求する。
 */

#include <trykernel.h>
#include <knldef.h>

/**
 * @brief 優先度ごとのレディキュー。
 *
 * 各要素は、対応するタスク優先度のREADY状態タスクをつなぐ
 * キューの先頭を指す。
 *
 * ready_queue[0]は最も高い優先度、
 * ready_queue[CNF_MAX_TSKPRI - 1]は最も低い優先度に対応する。
 */
TCB *ready_queue[CNF_MAX_TSKPRI];

/**
 * @brief 現在実行中のタスク。
 *
 * CPUが現在実行しているタスクのタスク管理ブロック（TCB）を指す。
 * コンテキスト切替時には、実行を停止するタスクの情報を保存し、
 * 次に実行するタスクへ更新される。
 */
TCB *cur_task;

/**
 * @brief 次に実行するタスク。
 *
 * スケジューラがレディキューから選択したタスクを指す。
 * cur_taskと異なるタスクが選択された場合、
 * ディスパッチャによるコンテキスト切替が要求される。
 */
TCB *sche_task;

/**
 * @brief ディスパッチャの実行状態。
 *
 * ディスパッチャが実行中かどうかを示す。
 * ディスパッチャ実行中の再入や重複したコンテキスト切替を
 * 防止するために使用する。
 *
 * @retval 0 ディスパッチャは実行中ではない。
 * @retval その他 ディスパッチャが実行中である。
 */
UW disp_running;

/* --------------------------------------------------
 ready_queue
    ↓ scheduler()が選択
 sche_task
    ↓
 dispatch()
    ↓ PendSV
 cur_taskを切り替え
   -------------------------------------------------- */
/**
 * @brief 次に実行するタスクを選択し、必要に応じてディスパッチを要求する。
 *
 * @details
 * 優先度の高いレディキューから順番に確認し、
 * 最初にタスクが登録されているキューの先頭タスクを
 * 次の実行タスクとして選択する。
 *
 * 実行可能なタスクが存在しない場合は、sche_taskにNULLを設定する。
 *
 * 選択されたタスクが現在実行中のタスクと異なり、
 * かつディスパッチャが実行中でない場合は、dispatch()を呼び出して
 * コンテキスト切替を要求する。
 *
 * @note
 * この関数はタスクのレジスタやスタックを直接切り替えない。
 * dispatch()によってPendSVを要求し、実際のコンテキスト切替は
 * PendSVハンドラで行われる。
 * --------------------------------------------------
 * ready_queue[0]から順に確認
 *        ↓
 * 最初に見つかったタスクをsche_taskへ設定
 *        ↓
 * sche_taskとcur_taskを比較
 *        ↓
 * 異なる場合
 *        ↓
 * dispatch()
 *        ↓
 * PendSV要求
 *        ↓
 * ISRまたは現在の処理が終了した後にコンテキスト切替
 */
void scheduler(void)
{
    INT		i;

    for(i = 0; i < CNF_MAX_TSKPRI; i++) {
        if( ready_queue[i] != NULL) break;
    }

    if(i < CNF_MAX_TSKPRI) {
        sche_task = ready_queue[i];
    } else {
        sche_task = NULL;    // 実行できるタスクは無い
    }
    if(sche_task != cur_task && !disp_running) {
        dispatch();         // ディスパッチャを実行
    }
}
