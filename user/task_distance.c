/**
 * @file    task_distance.c
 * @brief   距離センサーを定期的に読み取り、測定値を共有する。
 *
 * @details
 * VL53L1Xから距離を取得し、直近の測定値と移動平均を更新する。
 * 測定値は割り込み禁止区間で共有し、task_distance_get()から安全に取得できる。
 * 測定失敗時は共有データを無効化してモーターへ障害物状態を通知し、
 * 再試行間隔を空けて測定を再開する。有効な測定値はモーター制御にも通知する。
 */

#include <trykernel.h>
#include "vl53l1x.h"
#include "task_distance.h"
#include "motor.h"

#define DISTANCE_SAMPLE_INTERVAL_MS  100  //!< 正常測定後の次回測定までの待ち時間（ミリ秒）。
#define DISTANCE_RETRY_INTERVAL_MS   1000 //!< 測定失敗後の再試行までの待ち時間（ミリ秒）。

static volatile UH shared_average_mm;       //!< 移動平均距離（ミリメートル）。
static volatile UH shared_latest_mm;        //!< 直近の有効な測定距離（ミリメートル）。
static volatile UINT shared_sample_count;   //!< 移動平均の算出に使用したサンプル数。
static volatile BOOL shared_ready;          //!< 共有測定値が取得可能な場合にTRUE。

/**
 * @brief 距離測定タスクが共有する測定値を取得する。
 * @param[out] average_mm 移動平均距離（ミリメートル）の格納先。
 * @param[out] latest_mm 直近の有効な測定距離（ミリメートル）の格納先。
 * @param[out] sample_count 移動平均の算出に使用したサンプル数の格納先。
 * @retval TRUE 共有測定値を取得できた。
 * @retval FALSE 引数がNULL、または測定値が利用できない。
 * @details 読み取り中にタスク側の更新が割り込まないよう、共有値の取得を
 *          割り込み禁止区間内で行う。
 */
BOOL task_distance_get(UH *average_mm, UH *latest_mm, UINT *sample_count)
{
    UINT intsts;

    if((average_mm == NULL) || (latest_mm == NULL)
            || (sample_count == NULL)) return FALSE;

    DI(intsts);
    if(shared_ready == FALSE){
        EI(intsts);
        return FALSE;
    }
    *average_mm = shared_average_mm;
    *latest_mm = shared_latest_mm;
    *sample_count = shared_sample_count;
    EI(intsts);
    return TRUE;
}

/**
 * @brief 距離センサーを読み取り、移動平均と障害物状態を更新するタスク。
 * @param[in] stacd タスク起動コード（この関数では使用しない）。
 * @param[in] exinf タスク拡張情報（この関数では使用しない）。
 * @details センサー値を移動平均用リングバッファへ格納し、共有測定値を更新する。
 *          測定失敗時は共有値を無効にし、モーターへ測距不能を通知してから再試行する。
 *          正常時は所定の測定間隔、失敗時は再試行間隔で待機する。
 */
void task_distance(INT stacd, void *exinf)
{
    UH samples[DISTANCE_MOVING_AVERAGE_SIZE];
    UW sum = 0U;
    UINT count = 0U;
    UINT next = 0U;
    UH distance_mm;
    UINT intsts;
    UINT init_index;

    (void)stacd;
    (void)exinf;

    for(init_index = 0U; init_index < DISTANCE_MOVING_AVERAGE_SIZE;
            init_index++){
        samples[init_index] = 0U;
    }

    while(1){
        if(vl53l1x_read_distance(&distance_mm) == FALSE){
            DI(intsts);
            shared_ready = FALSE;
            EI(intsts);
            motor_obstacle_update(0U, FALSE);
            count = 0U;
            next = 0U;
            sum = 0U;
            (void)tk_dly_tsk(DISTANCE_RETRY_INTERVAL_MS);
            continue;
        }

        if(count == DISTANCE_MOVING_AVERAGE_SIZE){
            sum -= samples[next];
        }else{
            count++;
        }
        samples[next] = distance_mm;
        sum += distance_mm;
        next++;
        if(next == DISTANCE_MOVING_AVERAGE_SIZE) next = 0U;

        DI(intsts);
        shared_latest_mm = distance_mm;
        shared_average_mm = (UH)(sum / count);
        shared_sample_count = count;
        shared_ready = TRUE;
        EI(intsts);

        motor_obstacle_update(distance_mm, TRUE);

        (void)tk_dly_tsk(DISTANCE_SAMPLE_INTERVAL_MS);
    }
}
