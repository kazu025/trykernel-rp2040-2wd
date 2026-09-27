/*
 * MPU-6050 accelerometer and gyroscope driver
 */
#include <trykernel.h>
#include "i2c.h"
#include "mpu6050.h"

#define MPU6050_REG_ACCEL_XOUT_H  0x3BU
#define MPU6050_REG_INT_STATUS    0x3AU
#define MPU6050_REG_SMPLRT_DIV    0x19U
#define MPU6050_REG_GYRO_CONFIG   0x1BU
#define MPU6050_REG_CONFIG        0x1AU
#define MPU6050_REG_INT_ENABLE    0x38U
#define MPU6050_REG_PWR_MGMT_1    0x6BU
#define MPU6050_REG_WHO_AM_I      0x75U

static INT mpu6050_gyro_offset_x;
static INT mpu6050_gyro_offset_y;
static INT mpu6050_gyro_offset_z;

/*
 * 8ビットレジスタへ1つ書き込む
 */
static BOOL mpu6050_write_register(UB register_address, UB value)
{
    UB data[2];

    data[0] = register_address;
    data[1] = value;

    return i2c0_write(MPU6050_I2C_ADDR, data, 2U);
}

static BOOL mpu6050_read_register(UB register_address, UB *value)
{
    if(value == NULL){
        return FALSE;
    }
    return i2c0_write_read(
        MPU6050_I2C_ADDR, &register_address, 1U, value, 1U);
}

/*
 * ビッグエンディアンの16ビットデータを符号付き整数へ変換する
 */
static INT mpu6050_decode_s16(UB high, UB low)
{
    UINT value;

    value = ((UINT)high << 8) | (UINT)low;
    if((value & 0x8000U) != 0U){
        return (INT)value - 65536;
    }

    return (INT)value;
}

/*
 * WHO_AM_Iレジスタを読み出す
 */
BOOL mpu6050_read_who_am_i(UB *device_id)
{
    UB register_address;

    if(device_id == NULL){
        return FALSE;
    }

    register_address = MPU6050_REG_WHO_AM_I;

    return i2c0_write_read(
        MPU6050_I2C_ADDR,
        &register_address,
        1U,
        device_id,
        1U
    );
}

/*
 * このドライバで扱えるデバイスIDか確認する
 */
BOOL mpu6050_is_supported_device(UB device_id)
{
    return (device_id == MPU6050_WHO_AM_I)
        || (device_id == MPU6500_WHO_AM_I);
}

/*
 * スリープを解除する
 *
 * 初期設定では加速度レンジは±2g、ジャイロレンジは±250dps。
 */
BOOL mpu6050_init(void)
{
    /* X軸ジャイロPLL、DLPF有効、ジャイロ±250dpsを明示する。 */
    return mpu6050_write_register(MPU6050_REG_PWR_MGMT_1, 0x01U)
        && mpu6050_write_register(MPU6050_REG_GYRO_CONFIG, 0x00U)
        && mpu6050_write_register(MPU6050_REG_CONFIG, 0x03U);
}

// MPU測定完了を10HzでINT端子から通知する
BOOL mpu6050_enable_data_ready_interrupt(void)
{
    /* DLPF有効時の1kHz出力を100分周して10Hzにする */
    return mpu6050_write_register(MPU6050_REG_CONFIG, 0x03U)    // ディジタルローパスフィルタ：ノイズを抑える
        && mpu6050_write_register(MPU6050_REG_SMPLRT_DIV, 99U)  // 10Hz
        && mpu6050_write_register(MPU6050_REG_INT_ENABLE, 0x01U); // 割り込み有効
}
// 割り込み発生要因を取得
BOOL mpu6050_read_interrupt_status(UB *status)
{
    return mpu6050_read_register(MPU6050_REG_INT_STATUS, status);
}

/*
 * 加速度、温度、ジャイロの生データを連続読み出しする
 */
BOOL mpu6050_read_raw(mpu6050_raw_data_t *raw_data)
{
    UB register_address;
    UB data[14];

    if(raw_data == NULL){
        return FALSE;
    }

    register_address = MPU6050_REG_ACCEL_XOUT_H;
    if(i2c0_write_read(
            MPU6050_I2C_ADDR,
            &register_address,
            1U,
            data,
            14U) == FALSE){
        return FALSE;
    }

    raw_data->accel_x = mpu6050_decode_s16(data[0], data[1]);
    raw_data->accel_y = mpu6050_decode_s16(data[2], data[3]);
    raw_data->accel_z = mpu6050_decode_s16(data[4], data[5]);
    raw_data->temperature = mpu6050_decode_s16(data[6], data[7]);
    raw_data->gyro_x = mpu6050_decode_s16(data[8], data[9]);
    raw_data->gyro_y = mpu6050_decode_s16(data[10], data[11]);
    raw_data->gyro_z = mpu6050_decode_s16(data[12], data[13]);

    return TRUE;
}

/*
 * 静止中のジャイロ生データを平均し、ゼロ点オフセットとして保存する
 */
BOOL mpu6050_calibrate_gyro(
    UINT sample_count,
    RELTIM sample_period,
    INT *offset_x,
    INT *offset_y,
    INT *offset_z
)
{
    mpu6050_raw_data_t raw_data;
    D sum_x = 0;
    D sum_y = 0;
    D sum_z = 0;

    if((sample_count == 0U)
            || (offset_x == NULL)
            || (offset_y == NULL)
            || (offset_z == NULL)){
        return FALSE;
    }

    if(mpu6050_init() == FALSE){
        return FALSE;
    }

    /* 初期化直後の出力が落ち着いてから平均する。 */
    if(tk_dly_tsk(100) < E_OK) return FALSE;

    for(UINT i = 0U; i < sample_count; i++){
        if(mpu6050_read_raw(&raw_data) == FALSE){
            return FALSE;
        }

        sum_x += raw_data.gyro_x;
        sum_y += raw_data.gyro_y;
        sum_z += raw_data.gyro_z;

        if((sample_period > 0) && ((i + 1U) < sample_count)){
            if(tk_dly_tsk(sample_period) < E_OK){
                return FALSE;
            }
        }
    }

    mpu6050_gyro_offset_x = (INT)(sum_x / (D)sample_count);
    mpu6050_gyro_offset_y = (INT)(sum_y / (D)sample_count);
    mpu6050_gyro_offset_z = (INT)(sum_z / (D)sample_count);

    *offset_x = mpu6050_gyro_offset_x;
    *offset_y = mpu6050_gyro_offset_y;
    *offset_z = mpu6050_gyro_offset_z;

    return TRUE;
}

/*
 * 静止中のジャイロ零点と加速度ベクトル長の基準値を求める
 */
BOOL mpu6050_calibrate_motion(
    UINT sample_count,
    RELTIM sample_period,
    INT *offset_x,
    INT *offset_y,
    INT *offset_z,
    long long *accel_magnitude_squared
)
{
    mpu6050_raw_data_t raw_data;
    long long gyro_sum_x = 0;
    long long gyro_sum_y = 0;
    long long gyro_sum_z = 0;
    long long accel_magnitude_sum = 0;

    if((sample_count == 0U)
            || (offset_x == NULL)
            || (offset_y == NULL)
            || (offset_z == NULL)
            || (accel_magnitude_squared == NULL)){
        return FALSE;
    }

    if(mpu6050_init() == FALSE){
        return FALSE;
    }

    for(UINT i = 0U; i < sample_count; i++){
        if(mpu6050_read_raw(&raw_data) == FALSE){
            return FALSE;
        }

        gyro_sum_x += raw_data.gyro_x;
        gyro_sum_y += raw_data.gyro_y;
        gyro_sum_z += raw_data.gyro_z;
        accel_magnitude_sum +=
            ((long long)raw_data.accel_x * raw_data.accel_x)
            + ((long long)raw_data.accel_y * raw_data.accel_y)
            + ((long long)raw_data.accel_z * raw_data.accel_z);

        if((sample_period > 0) && ((i + 1U) < sample_count)){
            if(tk_dly_tsk(sample_period) < E_OK){
                return FALSE;
            }
        }
    }

    mpu6050_gyro_offset_x = (INT)(gyro_sum_x / sample_count);
    mpu6050_gyro_offset_y = (INT)(gyro_sum_y / sample_count);
    mpu6050_gyro_offset_z = (INT)(gyro_sum_z / sample_count);

    *offset_x = mpu6050_gyro_offset_x;
    *offset_y = mpu6050_gyro_offset_y;
    *offset_z = mpu6050_gyro_offset_z;
    *accel_magnitude_squared = accel_magnitude_sum / sample_count;

    return TRUE;
}

/*
 * 保存済みゼロ点オフセットを適用してミリdpsへ変換する
 */
BOOL mpu6050_get_gyro_milli_dps(
    const mpu6050_raw_data_t *raw_data,
    INT *gyro_x_milli_dps,
    INT *gyro_y_milli_dps,
    INT *gyro_z_milli_dps
)
{
    if((raw_data == NULL)
            || (gyro_x_milli_dps == NULL)
            || (gyro_y_milli_dps == NULL)
            || (gyro_z_milli_dps == NULL)){
        return FALSE;
    }

    *gyro_x_milli_dps =
        ((raw_data->gyro_x - mpu6050_gyro_offset_x) * 1000) / 131;
    *gyro_y_milli_dps =
        ((raw_data->gyro_y - mpu6050_gyro_offset_y) * 1000) / 131;
    *gyro_z_milli_dps =
        ((raw_data->gyro_z - mpu6050_gyro_offset_z) * 1000) / 131;

    return TRUE;
}
