/**
 * @file    ina226.h
 * @brief   INA226の識別定数と測定値取得APIを宣言する。
 * @details メーカー・デバイスID、バス電圧、シャント電圧および電流を
 *          取得するインターフェースを公開する。
 */

#ifndef INA226_H
#define INA226_H

#include <trykernel.h>

#define INA226_I2C_ADDR             0x40U //!< 通信に使用する7ビットI2Cアドレス。
#define INA226_EXPECTED_MANUFACTURER_ID  0x5449U //!< メーカーIDの期待値。
#define INA226_EXPECTED_DIE_ID           0x2260U //!< デバイスIDの期待値。

BOOL ina226_read_ids(UINT *manufacturer_id, UINT *die_id);
BOOL ina226_read_bus_voltage(UINT *raw_value, UW *voltage_mv);
BOOL ina226_read_shunt(
    INT *raw_value,
    INT *shunt_voltage_uv,
    INT *current_ua
);

#endif /* INA226_H */
