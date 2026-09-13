/*
 * INA226 current, voltage and power monitor driver
 */
#include <trykernel.h>
#include "i2c.h"
#include "ina226.h"

#define INA226_REG_MANUFACTURER_ID  0xFEU   // メーカーIDレジスタのアドレス
#define INA226_REG_DIE_ID           0xFFU   // デバイスIDレジスタのアドレス
#define INA226_REG_BUS_VOLTAGE      0x02U   // bus電圧レジスタのアドレス
#define INA226_REG_SHUNT_VOLTAGE    0x01U   // シャント抵抗両端電圧レジスタのアドレス
#define INA226_SHUNT_RESISTANCE_MILLIOHM  100

/*
 * ビッグエンディアンの16ビットレジスタを読み出す
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

/*
 * Manufacturer IDとDie IDを読み出す
 */
BOOL ina226_read_ids(UINT *manufacturer_id, UINT *die_id)
{
    if((manufacturer_id == NULL) || (die_id == NULL)) return FALSE;

    return ina226_read_register(
            INA226_REG_MANUFACTURER_ID, manufacturer_id)
        && ina226_read_register(INA226_REG_DIE_ID, die_id);
}

/*
 * バス電圧の生データとmV換算値を読み出す
 *
 * INA226のバス電圧は1LSBあたり1.25mV。
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

/*
 * シャント電圧と、基板上のR100（0.1Ω）から求めた電流を読み出す
 *
 * シャント電圧: 1LSB = 2.5uV
 * 電流: uA = uV * 1000 / mΩ
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
