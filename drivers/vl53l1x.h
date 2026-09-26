#ifndef VL53L1X_H
#define VL53L1X_H

#include <trykernel.h>

#define VL53L1X_I2C_ADDR  0x29U

BOOL vl53l1x_init(void);
BOOL vl53l1x_read_distance(UH *distance_mm);

#endif /* VL53L1X_H */
