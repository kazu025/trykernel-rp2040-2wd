#ifndef INA226_H
#define INA226_H

#include <trykernel.h>

#define INA226_I2C_ADDR             0x40U
#define INA226_EXPECTED_MANUFACTURER_ID  0x5449U
#define INA226_EXPECTED_DIE_ID           0x2260U

BOOL ina226_read_ids(UINT *manufacturer_id, UINT *die_id);
BOOL ina226_read_bus_voltage(UINT *raw_value, UW *voltage_mv);
BOOL ina226_read_shunt(
    INT *raw_value,
    INT *shunt_voltage_uv,
    INT *current_ua
);

#endif /* INA226_H */
