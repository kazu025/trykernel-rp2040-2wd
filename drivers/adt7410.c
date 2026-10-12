/**
 * @file adt7410.c
 * @brief ADT7410温度センサーのI2Cドライバー。
 *
 * ADT7410のレジスタ読み書き、デバイス情報の取得、
 * 温度分解能の設定、および温度データの読み出しと換算を行う。
 * 温度はミリ℃単位で取得できる。
 */
#include <trykernel.h>
#include "i2c.h"
#include "adt7410.h"

#define ADT7410_REG_TEMPERATURE  0x00U
#define ADT7410_REG_CONFIGURATION 0x03U
#define ADT7410_REG_ID            0x0BU
#define ADT7410_CONFIG_RESOLUTION  (1U << 7)

/**
 * @brief ADT7410の8ビットレジスタを読み出す。
 *
 * @param register_address 読み出すレジスタアドレス。
 * @param value 読み出した値の格納先。NULLの場合はFALSEを返す。
 * @return 読み出しに成功した場合はTRUE、失敗した場合はFALSE。
 */
static BOOL adt7410_read_register(UB register_address, UB *value)
{
    if(value == NULL){
        return FALSE;
    }

    return i2c0_write_read(
        ADT7410_I2C_ADDR,
        &register_address,
        1U,
        value,
        1U
    );
}
/**
 * @brief ADT7410の8ビットレジスタに書き込む。
 *
 * @param register_address 書き込み先のレジスタアドレス。
 * @param value 書き込む値。
 * @return 書き込みに成功した場合はTRUE、失敗した場合はFALSE。
 */
static BOOL adt7410_write_register(UB register_address, UB value)
{
    UB data[2];

    data[0] = register_address;
    data[1] = value;

    return i2c0_write(ADT7410_I2C_ADDR, data, 2U);
}
/**
 * @brief ADT7410の温度分解能を設定する。
 *
 * 分解能設定を書き込んだ後、設定レジスタを読み戻して確認する。
 *
 * @param resolution_16bit TRUEなら16ビット、FALSEなら13ビットに設定する。
 * @return 設定と読み戻し確認に成功した場合はTRUE、失敗した場合はFALSE。
 */
BOOL adt7410_set_resolution(BOOL resolution_16bit)
{
    UB configuration;

    if(adt7410_read_register(
            ADT7410_REG_CONFIGURATION,
            &configuration) == FALSE){
        return FALSE;
    }

    if(resolution_16bit != FALSE){
        configuration |= ADT7410_CONFIG_RESOLUTION;
    }else{
        configuration &= (UB)~ADT7410_CONFIG_RESOLUTION;
    }

    if(adt7410_write_register(
            ADT7410_REG_CONFIGURATION,
            configuration) == FALSE){
        return FALSE;
    }

    /* 書き込んだ設定を読み戻して確認する */
    if(adt7410_read_register(
            ADT7410_REG_CONFIGURATION,
            &configuration) == FALSE){
        return FALSE;
    }

    return (((configuration & ADT7410_CONFIG_RESOLUTION) != 0U)
        == (resolution_16bit != FALSE));
}
/**
 * @brief ADT7410のデバイスIDと設定レジスタを読み出す。
 *
 * @param device_id デバイスIDの格納先。
 * @param configuration 設定レジスタ値の格納先。
 * @return 両方の読み出しに成功した場合はTRUE、引数がNULLまたは読み出しに失敗した場合はFALSE。
 */
BOOL adt7410_read_device_info(UB *device_id, UB *configuration)
{
    if((device_id == NULL) || (configuration == NULL)){
        return FALSE;
    }

    if(adt7410_read_register(ADT7410_REG_ID, device_id) == FALSE){
        return FALSE;
    }

    if(adt7410_read_register(
            ADT7410_REG_CONFIGURATION,
            configuration) == FALSE){
        return FALSE;
    }

    return TRUE;
}
/**
 * @brief ADT7410の温度レジスタ値と現在の分解能を読み出す。
 *
 * 温度レジスタの上位・下位バイトを結合した生データと、
 * 設定レジスタから取得した分解能モードを返す。
 *
 * @param raw_temperature 16ビットの温度レジスタ値の格納先。
 * @param resolution_16bit 現在の分解能。16ビットならTRUE、13ビットならFALSE。
 * @return 読み出しに成功した場合はTRUE、引数がNULLまたはI2C通信に失敗した場合はFALSE。
 */
BOOL adt7410_read_raw_temperature(
    UINT *raw_temperature,
    BOOL *resolution_16bit
)
{
    UB register_address;
    UB data[2];
    UB configuration;

    if((raw_temperature == NULL) || (resolution_16bit == NULL)){
        return FALSE;
    }

    if(adt7410_read_register(
            ADT7410_REG_CONFIGURATION,
            &configuration) == FALSE){
        return FALSE;
    }

    register_address = ADT7410_REG_TEMPERATURE;

    if(i2c0_write_read(
            ADT7410_I2C_ADDR,
            &register_address,
            1U,
            data,
            2U) == FALSE){
        return FALSE;
    }

    *raw_temperature =
        ((UINT)data[0] << 8)
        | (UINT)data[1];
    *resolution_16bit =
        ((configuration & ADT7410_CONFIG_RESOLUTION) != 0U);

    return TRUE;
}
/**
 * @brief ADT7410から温度を読み出し、ミリ℃単位で返す。
 *
 * 温度レジスタ値を符号付き温度として変換する。
 * 13ビットモードでは、状態フラグを含む下位3ビットを除いて換算する。
 *
 * @param temperature_milli_c 温度の格納先（ミリ℃）。
 * @return 読み出しと換算に成功した場合はTRUE、引数がNULLまたはI2C通信に失敗した場合はFALSE。
 *
 * @note 例：25.125℃は25125、-5.500℃は-5500として返る。
 */
BOOL adt7410_read_temperature(INT *temperature_milli_c)
{
    UINT raw_unsigned;
    INT raw_signed;
    BOOL resolution_16bit;

    if(temperature_milli_c == NULL){
        return FALSE;
    }

    if(adt7410_read_raw_temperature(
            &raw_unsigned,
            &resolution_16bit) == FALSE){
        return FALSE;
    }

    /* 13ビット形式では下位3ビットは状態フラグ */
    if(resolution_16bit == FALSE){
        raw_unsigned &= 0xFFF8U;
    }

    /* 16ビット2の補数をINTへ変換 */
    if((raw_unsigned & 0x8000U) != 0U){
        raw_signed = (INT)raw_unsigned - 65536;
    }else{
        raw_signed = (INT)raw_unsigned;
    }

    if(raw_signed >= 0){
        *temperature_milli_c = (raw_signed * 1000 + 64) / 128;
    }else{
        *temperature_milli_c = -(((-raw_signed) * 1000 + 64) / 128);
    }
    return TRUE;
}
