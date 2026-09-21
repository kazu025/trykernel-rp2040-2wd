#include <trykernel.h>
#include "gpio.h"
#include "mpu6050.h"
#include "task_mpuirq.h"
#include "task_motionled.h"
#include "task_flashlog.h"
#include "uart_tx.h"

#define MPU_INT_PIN          6U
#define MPU_EVENT_DATA_READY (1U << 0)
#define MOTION_ACCEL_SQUARED_THRESHOLD  40000000LL
#define MOTION_GYRO_MILLI_DPS_THRESHOLD 5000
#define MOTION_ACTIVE_SAMPLE_COUNT      2U
#define MOTION_STILL_SAMPLE_COUNT       10U

static ID mpu_flgid;
static volatile UW mpu_sample_count;
static volatile UW mpu_error_count;
static volatile BOOL motion_enabled;
static volatile long long motion_accel_reference_squared;
static BOOL motion_detected;
static UINT motion_active_count;
static UINT motion_still_count;

static INT absolute_int(INT value)
{
    return (value < 0) ? -value : value;
}

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

static void mpu_data_ready_notify_from_isr(void)
{
    if(mpu_flgid > 0){
        (void)tk_set_flg(mpu_flgid, MPU_EVENT_DATA_READY);
    }
}

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

void task_mpuirq_motion_stop(void)
{
    motion_enabled = FALSE;
    task_motionled_set_off();
}

BOOL task_mpuirq_motion_is_enabled(void)
{
    return motion_enabled;
}

UW task_mpuirq_sample_count_get(void)
{
    return mpu_sample_count;
}

UW task_mpuirq_error_count_get(void)
{
    return mpu_error_count;
}

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
