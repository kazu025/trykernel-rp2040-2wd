/**
 * @file    mpu6050.c
 * @brief   MPU6050の加速度・角速度データ取得とジャイロ校正を行う。
 * @details I2Cによるレジスター操作、初期設定、データ準備割り込み設定、
 *          生データ取得を提供する。静止時のジャイロオフセットを保持し、
 *          補正した角速度への変換と動作検出用の加速度基準値算出を行う。
 */
#include <trykernel.h>
#include "i2c.h"
#include "mpu6050.h"

#define MPU6050_REG_ACCEL_XOUT_H  0x3BU //!< X軸加速度上位バイト。14バイト連続読み出しの開始位置。
#define MPU6050_REG_INT_STATUS    0x3AU //!< 割り込み状態レジスター。
#define MPU6050_REG_SMPLRT_DIV    0x19U //!< サンプルレート分周設定レジスター。
#define MPU6050_REG_GYRO_CONFIG   0x1BU //!< ジャイロ測定範囲などの設定レジスター。
#define MPU6050_REG_CONFIG        0x1AU //!< デジタルローパスフィルターなどの設定レジスター。
#define MPU6050_REG_INT_ENABLE    0x38U //!< 割り込み有効化レジスター。
#define MPU6050_REG_PWR_MGMT_1    0x6BU //!< 電源管理とクロック選択のレジスター。
#define MPU6050_REG_WHO_AM_I      0x75U //!< デバイス識別レジスター。

static INT mpu6050_gyro_offset_x; //!< X軸ジャイロのゼロ点オフセット（生データ単位、初期値0）。
static INT mpu6050_gyro_offset_y; //!< Y軸ジャイロのゼロ点オフセット（生データ単位、初期値0）。
static INT mpu6050_gyro_offset_z; //!< Z軸ジャイロのゼロ点オフセット（生データ単位、初期値0）。

/**
 * @brief 指定レジスターへ1バイトを書き込む。
 * @param[in] register_address 書き込み先レジスターのアドレス。
 * @param[in] value 書き込む値。
 * @retval TRUE I2C書き込みに成功した。
 * @retval FALSE I2C書き込みに失敗した。
 */
static BOOL mpu6050_write_register(UB register_address, UB value)
{
    UB data[2];

    data[0] = register_address;
    data[1] = value;

    return i2c0_write(MPU6050_I2C_ADDR, data, 2U);
}

/**
 * @brief 指定レジスターから1バイトを読み出す。
 * @param[in] register_address 読み出すレジスターのアドレス。
 * @param[out] value 読み出した値の格納先。
 * @retval TRUE 読み出しに成功した。
 * @retval FALSE valueがNULL、またはI2C通信に失敗した。
 */
static BOOL mpu6050_read_register(UB register_address, UB *value)
{
    if(value == NULL){
        return FALSE;
    }
    return i2c0_write_read(
        MPU6050_I2C_ADDR, &register_address, 1U, value, 1U);
}

/**
 * @brief 上位・下位バイトから16ビット符号付き整数値を復元する。
 * @param[in] high 上位バイト。
 * @param[in] low 下位バイト。
 * @return 2の補数として解釈した値（-32768～32767）。
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

/**
 * @brief WHO_AM_IレジスターからデバイスIDを読み出す。
 * @param[out] device_id デバイスIDの格納先。
 * @retval TRUE 読み出しに成功した。
 * @retval FALSE device_idがNULL、またはI2C通信に失敗した。
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

/**
 * @brief デバイスIDがドライバの受け付ける値か判定する。
 * @param[in] device_id WHO_AM_Iから取得した値。
 * @retval TRUE MPU6050またはMPU6500の識別値である。
 * @retval FALSE 上記以外の値である。
 */
BOOL mpu6050_is_supported_device(UB device_id)
{
    return (device_id == MPU6050_WHO_AM_I)
        || (device_id == MPU6500_WHO_AM_I);
}

/**
 * @brief スリープを解除し、クロック・ジャイロ範囲・フィルターを設定する。
 * @details X軸ジャイロPLLを選択し、ジャイロを±250 dps、CONFIGを0x03に設定する。
 *          加速度範囲や保存済みジャイロオフセットは変更しない。
 * @retval TRUE すべてのレジスター書き込みに成功した。
 * @retval FALSE I2C書き込みに失敗した。
 */
BOOL mpu6050_init(void)
{
    /* X軸ジャイロPLL、DLPF有効、ジャイロ±250dpsを明示する。 */
    return mpu6050_write_register(MPU6050_REG_PWR_MGMT_1, 0x01U)
        && mpu6050_write_register(MPU6050_REG_GYRO_CONFIG, 0x00U)
        && mpu6050_write_register(MPU6050_REG_CONFIG, 0x03U);
}

/**
 * @brief 10 Hzのデータ準備完了割り込みを有効にする。
 * @details フィルター設定とサンプルレート分周値99を書き込み、割り込みを有効化する。
 * @retval TRUE 設定に成功した。
 * @retval FALSE I2C書き込みに失敗した。
 */
BOOL mpu6050_enable_data_ready_interrupt(void)
{
    /* DLPF有効時の1kHz出力を100分周して10Hzにする */
    return mpu6050_write_register(MPU6050_REG_CONFIG, 0x03U)    // ディジタルローパスフィルタ：ノイズを抑える
        && mpu6050_write_register(MPU6050_REG_SMPLRT_DIV, 99U)  // 10Hz
        && mpu6050_write_register(MPU6050_REG_INT_ENABLE, 0x01U); // 割り込み有効
}
/**
 * @brief 割り込み状態レジスターを読み出す。
 * @param[out] status 割り込み状態の格納先。
 * @retval TRUE 読み出しに成功した。
 * @retval FALSE statusがNULL、またはI2C通信に失敗した。
 */
BOOL mpu6050_read_interrupt_status(UB *status)
{
    return mpu6050_read_register(MPU6050_REG_INT_STATUS, status);
}

/**
 * @brief 加速度・温度・ジャイロの生データを14バイト連続で読み出す。
 * @param[out] raw_data 各値を符号付き整数へ変換して格納する領域。
 * @retval TRUE すべての値を取得した。
 * @retval FALSE raw_dataがNULL、またはI2C通信に失敗した。
 * @note 物理単位への変換やオフセット補正は行わない。失敗時は出力を変更しない。
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

/**
 * @brief 静止中のジャイロ生データを平均し、ゼロ点オフセットを保存する。
 * @param[in] sample_count 平均に使用するサンプル数。1以上を指定する。
 * @param[in] sample_period サンプル間の待ち時間（ミリ秒）。0以下なら待機しない。
 * @param[out] offset_x X軸オフセットの格納先（生データ単位）。
 * @param[out] offset_y Y軸オフセットの格納先（生データ単位）。
 * @param[out] offset_z Z軸オフセットの格納先（生データ単位）。
 * @retval TRUE 校正に成功し、内部オフセットと出力引数を更新した。
 * @retval FALSE 引数不正、初期化・読み取り・待機のいずれかに失敗した。
 * @details 初期化後に100 ms待機してから測定する。失敗時は以前のオフセットを保持する。
 * @note センサーを静止させ、待機可能なタスクから呼び出すこと。
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

/**
 * @brief 静止時のジャイロオフセットと加速度ベクトル二乗和の平均を求める。
 * @param[in] sample_count 平均に使用するサンプル数。1以上を指定する。
 * @param[in] sample_period サンプル間の待ち時間（ミリ秒）。0以下なら待機しない。
 * @param[out] offset_x X軸ジャイロオフセットの格納先（生データ単位）。
 * @param[out] offset_y Y軸ジャイロオフセットの格納先（生データ単位）。
 * @param[out] offset_z Z軸ジャイロオフセットの格納先（生データ単位）。
 * @param[out] accel_magnitude_squared 加速度3軸の生データ二乗和を平均した基準値の格納先。
 * @retval TRUE 校正に成功し、内部オフセットと出力引数を更新した。
 * @retval FALSE 引数不正、初期化・読み取り・待機のいずれかに失敗した。
 * @note センサーを静止させて呼び出すこと。失敗時は以前のオフセットを保持する。
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

/**
 * @brief 保存済みオフセットを差し引き、ジャイロ生データを角速度へ変換する。
 * @param[in] raw_data 変換元のジャイロ生データを含む構造体。
 * @param[out] gyro_x_milli_dps X軸角速度の格納先（mdps）。
 * @param[out] gyro_y_milli_dps Y軸角速度の格納先（mdps）。
 * @param[out] gyro_z_milli_dps Z軸角速度の格納先（mdps）。
 * @retval TRUE 変換結果を格納した。
 * @retval FALSE いずれかの引数がNULLである。
 * @details ±250 dps設定に対応する感度131を使用する。1000 mdpsは1度/秒に相当する。
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
