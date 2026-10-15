/**
 * @file    vl53l1x.h
 * @brief   VL53L1X距離センサードライバの定数とAPIを宣言する。
 * @details センサー初期化、距離取得、および直近の測定試行の診断情報を
 *          取得するインターフェースを公開する。
 */

#ifndef VL53L1X_H
#define VL53L1X_H

#include <trykernel.h>

#define VL53L1X_I2C_ADDR  0x29U

BOOL vl53l1x_init(void);
BOOL vl53l1x_read_distance(UH *distance_mm);
BOOL vl53l1x_get_last_result(UH *raw_mm, UB *raw_status);

#endif /* VL53L1X_H */
