/*
 * Minimal VL53L1X driver for TryKernel.
 *
 * The default configuration is derived from STMicroelectronics VL53L1X
 * Ultra Lite Driver.
 *
 * Copyright (c) 2018 STMicroelectronics. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 * 1. Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of STMicroelectronics nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES ARE DISCLAIMED. IN NO EVENT SHALL THE
 * COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH
 * DAMAGE.
 */
#include <trykernel.h>
#include "i2c.h"
#include "vl53l1x.h"

#define VL53L1X_REG_VHV_TIMEOUT       0x0008U
#define VL53L1X_REG_GPIO_MUX_CTRL     0x0030U
#define VL53L1X_REG_GPIO_STATUS       0x0031U
#define VL53L1X_REG_INTERRUPT_CLEAR   0x0086U
#define VL53L1X_REG_MODE_START        0x0087U
#define VL53L1X_REG_DISTANCE_MM       0x0096U
#define VL53L1X_REG_BOOT_STATE        0x00E5U
#define VL53L1X_REG_MODEL_ID          0x010FU
#define VL53L1X_MODEL_ID              0xEACCU
#define VL53L1X_READY_RETRY_COUNT     250U

static BOOL initialized;

static const UB default_configuration[] = {
    0x00, 0x00, 0x00, 0x01, 0x02, 0x00, 0x02, 0x08,
    0x00, 0x08, 0x10, 0x01, 0x01, 0x00, 0x00, 0x00,
    0x00, 0xff, 0x00, 0x0f, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x20, 0x0b, 0x00, 0x00, 0x02, 0x0a, 0x21,
    0x00, 0x00, 0x05, 0x00, 0x00, 0x00, 0x00, 0xc8,
    0x00, 0x00, 0x38, 0xff, 0x01, 0x00, 0x08, 0x00,
    0x00, 0x01, 0xcc, 0x0f, 0x01, 0xf1, 0x0d, 0x01,
    0x68, 0x00, 0x80, 0x08, 0xb8, 0x00, 0x00, 0x00,
    0x00, 0x0f, 0x89, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x01, 0x0f, 0x0d, 0x0e, 0x0e, 0x00,
    0x00, 0x02, 0xc7, 0xff, 0x9b, 0x00, 0x00, 0x00,
    0x01, 0x00, 0x00
};

_Static_assert(sizeof(default_configuration) == 91U,
               "VL53L1X default configuration size mismatch");

static BOOL write_register(UH register_address, UB value)
{
    UB data[3];
    data[0] = (UB)(register_address >> 8);
    data[1] = (UB)register_address;
    data[2] = value;
    return i2c0_write(VL53L1X_I2C_ADDR, data, 3U);
}

static BOOL read_registers(UH register_address, UB *data, UINT size)
{
    UB address[2];
    address[0] = (UB)(register_address >> 8);
    address[1] = (UB)register_address;
    return i2c0_write_read(VL53L1X_I2C_ADDR, address, 2U, data, size);
}

static BOOL read_register(UH register_address, UB *value)
{
    return read_registers(register_address, value, 1U);
}

static BOOL read_word(UH register_address, UH *value)
{
    UB data[2];
    if(read_registers(register_address, data, 2U) == FALSE) return FALSE;
    *value = (UH)(((UH)data[0] << 8) | (UH)data[1]);
    return TRUE;
}

static BOOL wait_data_ready(void)
{
    UINT retry;
    UB mux;
    UB status;
    UB ready_level;

    if(read_register(VL53L1X_REG_GPIO_MUX_CTRL, &mux) == FALSE) return FALSE;
    ready_level = (UB)(((mux & 0x10U) == 0U) ? 1U : 0U);

    for(retry = 0U; retry < VL53L1X_READY_RETRY_COUNT; retry++){
        if(read_register(VL53L1X_REG_GPIO_STATUS, &status) == FALSE) return FALSE;
        if((status & 0x01U) == ready_level) return TRUE;
        (void)tk_dly_tsk(2);
    }
    return FALSE;
}

BOOL vl53l1x_init(void)
{
    UINT retry;
    UINT index;
    UB boot_state;
    UH model_id;

    initialized = FALSE;
    for(retry = 0U; retry < 50U; retry++){
        if(read_register(VL53L1X_REG_BOOT_STATE, &boot_state) != FALSE
                && boot_state != 0U) break;
        (void)tk_dly_tsk(2);
    }
    if(retry == 50U) return FALSE;
    if(read_word(VL53L1X_REG_MODEL_ID, &model_id) == FALSE
            || model_id != VL53L1X_MODEL_ID) return FALSE;

    for(index = 0U; index < sizeof(default_configuration); index++){
        if(write_register((UH)(0x002DU + index),
                default_configuration[index]) == FALSE) return FALSE;
    }

    if(write_register(VL53L1X_REG_MODE_START, 0x40U) == FALSE) return FALSE;
    if(wait_data_ready() == FALSE) return FALSE;
    if(write_register(VL53L1X_REG_INTERRUPT_CLEAR, 0x01U) == FALSE) return FALSE;
    if(write_register(VL53L1X_REG_MODE_START, 0x00U) == FALSE) return FALSE;
    if(write_register(VL53L1X_REG_VHV_TIMEOUT, 0x09U) == FALSE) return FALSE;
    if(write_register(0x000BU, 0x00U) == FALSE) return FALSE;

    initialized = TRUE;
    return TRUE;
}

BOOL vl53l1x_read_distance(UH *distance_mm)
{
    BOOL result = FALSE;

    if(distance_mm == NULL) return FALSE;
    if(initialized == FALSE && vl53l1x_init() == FALSE) return FALSE;
    if(write_register(VL53L1X_REG_MODE_START, 0x40U) == FALSE) return FALSE;
    if(wait_data_ready() != FALSE){
        result = read_word(VL53L1X_REG_DISTANCE_MM, distance_mm);
        (void)write_register(VL53L1X_REG_INTERRUPT_CLEAR, 0x01U);
    }
    (void)write_register(VL53L1X_REG_MODE_START, 0x00U);
    return result;
}
