/**
 * @file    mpu6050.h
 * @brief   MPU6050ドライバの識別定数、測定データ型、APIを宣言する。
 * @details 初期化、割り込み設定、生データ取得、ジャイロ校正、角速度変換の
 *          インターフェースを公開する。
 */

#ifndef MPU6050_H
#define MPU6050_H

#include <trykernel.h>

#define MPU6050_I2C_ADDR  0x68U //!< 通信に使用する7ビットI2Cアドレス。
#define MPU6050_WHO_AM_I   0x68U //!< MPU6050として受け付けるデバイスID。
#define MPU6500_WHO_AM_I   0x70U //!< MPU6500として受け付けるデバイスID。

typedef struct {
    INT accel_x;
    INT accel_y;
    INT accel_z;
    INT temperature;
    INT gyro_x;
    INT gyro_y;
    INT gyro_z;
} mpu6050_raw_data_t;

BOOL mpu6050_read_who_am_i(UB *device_id);
BOOL mpu6050_is_supported_device(UB device_id);
BOOL mpu6050_init(void);
BOOL mpu6050_read_raw(mpu6050_raw_data_t *raw_data);
BOOL mpu6050_enable_data_ready_interrupt(void);
BOOL mpu6050_read_interrupt_status(UB *status);
BOOL mpu6050_calibrate_gyro(
    UINT sample_count,
    RELTIM sample_period,
    INT *offset_x,
    INT *offset_y,
    INT *offset_z
);
BOOL mpu6050_calibrate_motion(
    UINT sample_count,
    RELTIM sample_period,
    INT *offset_x,
    INT *offset_y,
    INT *offset_z,
    long long *accel_magnitude_squared
);
BOOL mpu6050_get_gyro_milli_dps(
    const mpu6050_raw_data_t *raw_data,
    INT *gyro_x_milli_dps,
    INT *gyro_y_milli_dps,
    INT *gyro_z_milli_dps
);

#endif /* MPU6050_H */
