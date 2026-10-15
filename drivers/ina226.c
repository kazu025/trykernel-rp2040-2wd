/**
 * @file    ina226.c
 * @brief   INA226の識別情報、バス電圧、シャント電圧と電流を取得する。
 * @details I2Cで16ビットレジスターを読み出し、電圧を整数単位へ変換する。
 *          電流は基板上のR100（0.1 Ω）シャント抵抗を前提に生データから算出する。
 */
#include <trykernel.h>
#include "i2c.h"
#include "ina226.h"

#define INA226_REG_MANUFACTURER_ID  0xFEU //!< メーカーIDレジスターのアドレス。
#define INA226_REG_DIE_ID           0xFFU //!< デバイスIDレジスターのアドレス。
#define INA226_REG_BUS_VOLTAGE      0x02U //!< バス電圧レジスターのアドレス。
#define INA226_REG_SHUNT_VOLTAGE    0x01U //!< シャント抵抗両端電圧レジスターのアドレス。
#define INA226_SHUNT_RESISTANCE_MILLIOHM  100 //!< 想定するシャント抵抗値（mΩ）。現在の電流換算式では直接参照しない。

/**
 * @brief 上位バイト先行の16ビットレジスター値を読み出す。
 * @param[in] register_address 読み出すレジスターのアドレス。下位8ビットを使用する。
 * @param[out] value 読み出した符号なし16ビット値の格納先。
 * @retval TRUE 読み出しに成功した。
 * @retval FALSE valueがNULL、またはI2C通信に失敗した。
 * @note 失敗時は出力値を変更しない。
 */
static BOOL ina226_read_register(UINT register_address, UINT *value)
{
    UB address;
    UB data[2];

    if(value == NULL) return FALSE;

    address = (UB)register_address;
    if(i2c0_write_read(
            INA226_I2C_ADDR,
            &address,
            1U,
            data,
            2U) == FALSE){
        return FALSE;
    }

    *value = ((UINT)data[0] << 8) | (UINT)data[1];
    return TRUE;
}

/**
 * @brief メーカーIDとデバイスIDを読み出す。
 * @param[out] manufacturer_id メーカーIDの格納先。
 * @param[out] die_id デバイスIDの格納先。
 * @retval TRUE 両方のIDを読み出した。
 * @retval FALSE 引数がNULL、またはいずれかの読み出しに失敗した。
 * @note IDと期待値の照合は行わない。デバイスIDの読み出し失敗時も、
 *       先に取得したメーカーIDは出力に反映される。
 */
BOOL ina226_read_ids(UINT *manufacturer_id, UINT *die_id)
{
    if((manufacturer_id == NULL) || (die_id == NULL)) return FALSE;

    return ina226_read_register(
            INA226_REG_MANUFACTURER_ID, manufacturer_id)
        && ina226_read_register(INA226_REG_DIE_ID, die_id);
}

/**
 * @brief バス電圧の生データとミリボルト換算値を取得する。
 * @param[out] raw_value バス電圧レジスターの生データ格納先。
 * @param[out] voltage_mv バス電圧（mV）の格納先。
 * @retval TRUE 読み出しと換算に成功した。
 * @retval FALSE 引数がNULL、またはI2C通信に失敗した。
 * @details 1 LSB = 1.25 mVとして換算し、1 mV未満を切り捨てる。
 * @note 失敗時は出力値を変更しない。
 */
BOOL ina226_read_bus_voltage(UINT *raw_value, UW *voltage_mv)
{
    UINT raw;

    if((raw_value == NULL) || (voltage_mv == NULL)) return FALSE;
    if(ina226_read_register(INA226_REG_BUS_VOLTAGE, &raw) == FALSE){
        return FALSE;
    }

    *raw_value = raw;
    // bus電圧は1LSB=1.25mVなので、raw * 1.25 = raw * 5 / 4
    *voltage_mv = ((UW)raw * 5U) / 4U;
    return TRUE;
}

/**
 * @brief シャント電圧を読み出し、R100抵抗を前提とした電流を算出する。
 * @param[out] raw_value 符号付き16ビットとして解釈した生データの格納先。
 * @param[out] shunt_voltage_uv シャント電圧（µV）の格納先。
 * @param[out] current_ua 電流（µA）の格納先。
 * @retval TRUE 読み出しと換算に成功した。
 * @retval FALSE 引数がNULL、またはI2C通信に失敗した。
 * @details 電圧は1 LSB = 2.5 µVとして整数へ変換する。電流は0.1 Ωの
 *          抵抗を前提に、生データへ25を乗じて算出する。負の値も保持する。
 * @note 失敗時は出力値を変更しない。
 */
BOOL ina226_read_shunt(
    INT *raw_value,
    INT *shunt_voltage_uv,
    INT *current_ua
)
{
    UINT raw_unsigned;
    INT raw_signed;

    if((raw_value == NULL)
            || (shunt_voltage_uv == NULL)
            || (current_ua == NULL)){
        return FALSE;
    }
    if(ina226_read_register(
            INA226_REG_SHUNT_VOLTAGE, &raw_unsigned) == FALSE){
        return FALSE;
    }
    // シャント電圧は符号付き16ビット整数なので、符号拡張する
    if((raw_unsigned & 0x8000U) != 0U){
        raw_signed = (INT)raw_unsigned - 65536;
    }else{
        raw_signed = (INT)raw_unsigned;
    }

    *raw_value = raw_signed;
    // シャント電圧は1LSB=2.5uVなので、raw * 2.5 = raw * 5 / 2
    *shunt_voltage_uv = (raw_signed * 5) / 2;
    /* R100では1LSB（2.5uV）が25uAに相当する */
    *current_ua = raw_signed * 25;
    return TRUE;
}
