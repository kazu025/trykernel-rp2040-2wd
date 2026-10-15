/**
 * @file    task_mpuirq.c
 * @brief   MPU6050のデータ準備割り込みを処理し、動作状態を検出する。
 *
 * @details
 * GPIO割り込みからイベントフラグでタスクへ通知し、MPU6050の割り込み状態と
 * 加速度・角速度データを読み取る。測定値から移動・停止を判定し、状態変化を
 * モーションLEDとFlashログへ通知する。サンプル数とエラー数も管理する。
 */

#include <trykernel.h>
#include "gpio.h"
#include "mpu6050.h"
#include "task_mpuirq.h"
#include "task_motionled.h"
#include "task_flashlog.h"
#include "uart_tx.h"

#define MPU_INT_PIN          6U       //!< MPU6050のデータ準備割り込みを接続するGPIOピン番号。
#define MPU_EVENT_DATA_READY (1U << 0) //!< MPU6050データ準備完了を示すイベントフラグ。
#define MOTION_ACCEL_SQUARED_THRESHOLD  40000000LL //!< 加速度ベクトルの大きさの変化を判定する二乗値しきい値。
#define MOTION_GYRO_MILLI_DPS_THRESHOLD 5000        //!< 動作判定に使う軸ごとの角速度しきい値（mdps）。
#define MOTION_ACTIVE_SAMPLE_COUNT      2U          //!< 動作開始と判定するために必要な連続サンプル数。
#define MOTION_STILL_SAMPLE_COUNT       10U         //!< 停止と判定するために必要な連続サンプル数。

static ID mpu_flgid;                                      //!< データ準備完了通知に使用するイベントフラグID。
static volatile UW mpu_sample_count;                      //!< 正常に読み取ったMPU6050サンプルの累積数。
static volatile UW mpu_error_count;                       //!< 割り込み待ちまたはセンサー読み取りのエラー数。
static volatile BOOL motion_enabled;                      //!< 動作検出が有効な場合にTRUE。
static volatile long long motion_accel_reference_squared; //!< 動作開始時に取得した加速度ベクトル二乗値の基準。
static BOOL motion_detected;                               //!< 現在、移動中と判定されている場合にTRUE。
static UINT motion_active_count;                           //!< 動作開始しきい値を超えた連続サンプル数。
static UINT motion_still_count;                            //!< 動作なしと判定された連続サンプル数。

/**
 * @brief 符号付き整数の絶対値を返す。
 * @param[in] value 対象の整数値。
 * @return valueの絶対値。
 */
static INT absolute_int(INT value)
{
    return (value < 0) ? -value : value;
}

/**
 * @brief MPU6050のサンプルから移動・停止状態を更新する。
 * @param[in] raw_data 加速度と角速度を含む生データ。
 * @details 加速度変化または角速度がしきい値を超える状態が所定回数続くと移動、
 *          動作なしの状態が所定回数続くと停止と判定する。状態変化時にはLED、
 *          UARTおよびFlashログへ通知する。
 */
static void motion_update(const mpu6050_raw_data_t *raw_data)
{
    long long accel_magnitude_squared;
    long long accel_difference;
    INT gyro_x_milli_dps;
    INT gyro_y_milli_dps;
    INT gyro_z_milli_dps;
    BOOL active;

    accel_magnitude_squared =
        ((long long)raw_data->accel_x * raw_data->accel_x)
        + ((long long)raw_data->accel_y * raw_data->accel_y)
        + ((long long)raw_data->accel_z * raw_data->accel_z);
    accel_difference =
        accel_magnitude_squared - motion_accel_reference_squared;
    if(accel_difference < 0){
        accel_difference = -accel_difference;
    }

    if(mpu6050_get_gyro_milli_dps(
            raw_data,
            &gyro_x_milli_dps,
            &gyro_y_milli_dps,
            &gyro_z_milli_dps) == FALSE){
        return;
    }

    active = (accel_difference > MOTION_ACCEL_SQUARED_THRESHOLD)
        || (absolute_int(gyro_x_milli_dps)
            > MOTION_GYRO_MILLI_DPS_THRESHOLD)
        || (absolute_int(gyro_y_milli_dps)
            > MOTION_GYRO_MILLI_DPS_THRESHOLD)
        || (absolute_int(gyro_z_milli_dps)
            > MOTION_GYRO_MILLI_DPS_THRESHOLD);

    if(active != FALSE){
        motion_still_count = 0U;
        if(motion_active_count < MOTION_ACTIVE_SAMPLE_COUNT){
            motion_active_count++;
        }
        if((motion_detected == FALSE)
                && (motion_active_count >= MOTION_ACTIVE_SAMPLE_COUNT)){
            motion_detected = TRUE;
            task_motionled_set_moving();
            uart_tx_send("Motion: MOVING\r\n");
            (void)task_flashlog_notify(
                FLASHLOG_EVENT_MOVING, mpu_sample_count);
        }
    }else{
        motion_active_count = 0U;
        if(motion_still_count < MOTION_STILL_SAMPLE_COUNT){
            motion_still_count++;
        }
        if((motion_detected != FALSE)
                && (motion_still_count >= MOTION_STILL_SAMPLE_COUNT)){
            motion_detected = FALSE;
            task_motionled_set_still();
            uart_tx_send("Motion: STOPPED\r\n");
            (void)task_flashlog_notify(
                FLASHLOG_EVENT_STOPPED, mpu_sample_count);
        }
    }
}

/**
 * @brief MPU6050のデータ準備割り込みをイベントフラグでタスクへ通知する。
 * @details GPIO割り込みハンドラから呼び出される。
 */
static void mpu_data_ready_notify_from_isr(void)
{
    if(mpu_flgid > 0){
        (void)tk_set_flg(mpu_flgid, MPU_EVENT_DATA_READY);
    }
}

/**
 * @brief MPU割り込み処理用イベントフラグと状態変数を初期化する。
 * @retval E_OK 初期化に成功した。
 * @return イベントフラグ作成に失敗した場合はTRYカーネルのエラーコード。
 */
ER task_mpuirq_init(void)
{
    T_CFLG cflg;

    cflg.flgatr = TA_TFIFO;
    cflg.iflgptn = 0U;
    mpu_flgid = tk_cre_flg(&cflg);
    if(mpu_flgid < E_OK){
        return (ER)mpu_flgid;
    }

    mpu_sample_count = 0U;
    mpu_error_count = 0U;
    motion_enabled = FALSE;
    motion_accel_reference_squared = 0;
    motion_detected = FALSE;
    motion_active_count = 0U;
    motion_still_count = 0U;
    return E_OK;
}

/**
 * @brief 指定した加速度を基準として動作検出を開始する。
 * @param[in] accel_magnitude_squared 静止基準とする加速度ベクトルの大きさの二乗。
 * @details 検出カウンタを初期化し、初期状態を停止としてLEDとFlashログへ通知する。
 */
void task_mpuirq_motion_start(long long accel_magnitude_squared)
{
    motion_accel_reference_squared = accel_magnitude_squared;
    motion_detected = FALSE;
    motion_active_count = 0U;
    motion_still_count = 0U;
    motion_enabled = TRUE;
    task_motionled_set_still();
    (void)task_flashlog_notify(FLASHLOG_EVENT_STOPPED, mpu_sample_count);
}

/**
 * @brief 動作検出を停止し、モーションLEDを消灯状態にする。
 */
void task_mpuirq_motion_stop(void)
{
    motion_enabled = FALSE;
    task_motionled_set_off();
}

/**
 * @brief 動作検出が有効かどうかを取得する。
 * @retval TRUE 動作検出が有効である。
 * @retval FALSE 動作検出が無効である。
 */
BOOL task_mpuirq_motion_is_enabled(void)
{
    return motion_enabled;
}

/**
 * @brief 正常に読み取ったMPU6050サンプル数を取得する。
 * @return 正常サンプルの累積数。
 */
UW task_mpuirq_sample_count_get(void)
{
    return mpu_sample_count;
}

/**
 * @brief MPU割り込み処理およびセンサー読み取りのエラー数を取得する。
 * @return エラーの累積数。
 */
UW task_mpuirq_error_count_get(void)
{
    return mpu_error_count;
}

/**
 * @brief MPU6050のデータ準備割り込みを待ち、測定値を処理するタスク。
 * @param[in] stacd タスク起動コード（この関数では使用しない）。
 * @param[in] exinf タスク拡張情報（この関数では使用しない）。
 * @details MPU6050とGPIO割り込みを初期化した後、通知ごとに割り込み状態と
 *          センサーデータを読み取る。正常データはサンプル数へ反映し、
 *          動作検出が有効な場合は状態判定へ渡す。
 */
void task_mpuirq(INT stacd, void *exinf)
{
    mpu6050_raw_data_t raw_data;
    UINT flgptn;
    UB int_status;

    (void)stacd;
    (void)exinf;

    if(mpu6050_init() == FALSE
            || mpu6050_enable_data_ready_interrupt() == FALSE
            || gpio_irq_init_rising(
                MPU_INT_PIN, mpu_data_ready_notify_from_isr) == FALSE){
        uart_tx_send("MPU data ready IRQ initialization error\r\n");
        /* タスク関数は通常のreturnで終了できない。 */
        tk_ext_tsk();
        return;
    }

    uart_tx_send("MPU data ready IRQ task start (GPIO6, 10Hz)\r\n");

    while(TRUE){
        if(tk_wai_flg(
                mpu_flgid,
                MPU_EVENT_DATA_READY,
                TWF_ORW | TWF_BITCLR,
                &flgptn,
                TMO_FEVR) < E_OK){
            mpu_error_count++;
            continue;
        }

        if(mpu6050_read_interrupt_status(&int_status) == FALSE
                || (int_status & 0x01U) == 0U
                || mpu6050_read_raw(&raw_data) == FALSE){
            mpu_error_count++;
            continue;
        }

        mpu_sample_count++;
        if(motion_enabled != FALSE){
            motion_update(&raw_data);
        }
    }
}
