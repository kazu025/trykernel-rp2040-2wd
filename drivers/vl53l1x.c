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
/**
 * @file    vl53l1x.c
 * @brief   VL53L1X距離センサーの初期化と距離取得を行う。
 * @details I2C経由で初期設定、測定開始、データ準備完了待ち、結果取得を行う。
 *          有効な距離をミリメートル単位で返し、診断用に直近の測定試行の
 *          距離とステータスも保持する。
 */

#include <trykernel.h>
#include "i2c.h"
#include "vl53l1x.h"

#define VL53L1X_REG_VHV_TIMEOUT       0x0008U
#define VL53L1X_REG_GPIO_MUX_CTRL     0x0030U
#define VL53L1X_REG_GPIO_STATUS       0x0031U
#define VL53L1X_REG_INTERRUPT_CLEAR   0x0086U
#define VL53L1X_REG_MODE_START        0x0087U
#define VL53L1X_REG_RANGE_STATUS      0x0089U
#define VL53L1X_RAW_RANGE_VALID       9U
#define VL53L1X_REG_DISTANCE_MM       0x0096U
#define VL53L1X_REG_BOOT_STATE        0x00E5U
#define VL53L1X_REG_MODEL_ID          0x010FU
#define VL53L1X_MODEL_ID              0xEACCU
#define VL53L1X_READY_RETRY_COUNT     250U

static BOOL initialized;
static volatile BOOL diagnostic_ready;
static volatile UH diagnostic_distance;
static volatile UB diagnostic_status;

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

/**
 * @brief 指定レジスターへ1バイトを書き込む。
 * @param[in] register_address 16ビットのレジスターアドレス。
 * @param[in] value 書き込む値。
 * @retval TRUE I2C書き込みに成功した。
 * @retval FALSE I2C書き込みに失敗した。
 */
static BOOL write_register(UH register_address, UB value)
{
    UB data[3];
    data[0] = (UB)(register_address >> 8);
    data[1] = (UB)register_address;
    data[2] = value;
    return i2c0_write(VL53L1X_I2C_ADDR, data, 3U);
}

/**
 * @brief 指定レジスターから連続したデータを読み出す。
 * @param[in] register_address 読み出し開始レジスターのアドレス。
 * @param[out] data sizeバイト以上の受信バッファ。
 * @param[in] size 読み出すバイト数。
 * @retval TRUE I2C読み出しに成功した。
 * @retval FALSE I2C読み出しに失敗した。
 */
static BOOL read_registers(UH register_address, UB *data, UINT size)
{
    UB address[2];
    address[0] = (UB)(register_address >> 8);
    address[1] = (UB)register_address;
    return i2c0_write_read(VL53L1X_I2C_ADDR, address, 2U, data, size);
}

/**
 * @brief 指定レジスターから1バイトを読み出す。
 * @param[in] register_address 読み出すレジスターのアドレス。
 * @param[out] value 読み出した値の格納先。
 * @retval TRUE 読み出しに成功した。
 * @retval FALSE 読み出しに失敗した。
 */
static BOOL read_register(UH register_address, UB *value)
{
    return read_registers(register_address, value, 1U);
}

/**
 * @brief 連続する2レジスターから上位バイト先行の16ビット値を読み出す。
 * @param[in] register_address 上位バイトのレジスターアドレス。
 * @param[out] value 変換した16ビット値の格納先。失敗時は変更しない。
 * @retval TRUE 読み出しに成功した。
 * @retval FALSE 読み出しに失敗した。
 */
static BOOL read_word(UH register_address, UH *value)
{
    UB data[2];
    if(read_registers(register_address, data, 2U) == FALSE) return FALSE;
    *value = (UH)(((UH)data[0] << 8) | (UH)data[1]);
    return TRUE;
}

/**
 * @brief センサーの測定データ準備完了を待つ。
 * @details GPIO出力の極性設定に従って状態を判定する。未完了の場合は
 *          2 ms待機し、最大VL53L1X_READY_RETRY_COUNT回確認する。
 * @retval TRUE データ準備完了を検出した。
 * @retval FALSE レジスター読み出し失敗、または確認回数の上限に達した。
 */
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

/**
 * @brief VL53L1Xの起動状態とモデルIDを確認し、初期設定を行う。
 * @details デフォルト設定を書き込み、初回の測定動作と追加設定を行った後、
 *          測定を停止した初期化済み状態にする。待機処理を含むためタスクから呼ぶ。
 * @retval TRUE 初期化が完了した。
 * @retval FALSE 起動待ち、モデルID確認、通信、または測定準備待ちに失敗した。
 */
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

/**
 * @brief 直近の測定試行で保存した診断用の距離とステータスを取得する。
 * @param[out] raw_mm センサーから読み出した距離（ミリメートル）の格納先。
 * @param[out] raw_status 測定ステータスの下位5ビットの格納先。
 * @retval TRUE 直近の測定試行で距離とステータスを読み出せた。
 * @retval FALSE 引数がNULL、または直近の測定試行で結果を読み出せなかった。
 * @note TRUEは距離の有効性を保証しない。測定ステータスも確認すること。
 *       両引数が非NULLなら、戻り値がFALSEの場合も保存値をコピーする。
 */
BOOL vl53l1x_get_last_result(UH *raw_mm, UB *raw_status)
{
    UINT intsts;
    BOOL ready;
    if(raw_mm == NULL || raw_status == NULL) return FALSE;
    DI(intsts);
    ready = diagnostic_ready;
    *raw_mm = diagnostic_distance;
    *raw_status = diagnostic_status;
    EI(intsts);
    return ready;
}

/**
 * @brief 測定を実行し、有効な距離をミリメートル単位で取得する。
 * @param[out] distance_mm 有効な距離の格納先。失敗時は変更しない。
 * @details 未初期化の場合は初期化を行い、測定開始後にデータ準備完了を待つ。
 *          生ステータスが9の場合のみ成功とし、取得結果を診断用に保存する。
 *          測定後は割り込みクリアと測定停止の処理を行う。
 * @retval TRUE 有効な距離を取得した。
 * @retval FALSE 引数がNULL、初期化・測定・通信の失敗、または測定値が無効。
 * @note 待機処理を含むためタスクから呼び出す。
 */
BOOL vl53l1x_read_distance(UH *distance_mm)
{
    BOOL result = FALSE;
    BOOL read_ok = FALSE;
    UH raw_mm = 0U;
    UB raw_status = 0U;
    UINT intsts;

    if(distance_mm == NULL) return FALSE;
    if(initialized == FALSE && vl53l1x_init() == FALSE) goto publish;
    if(write_register(VL53L1X_REG_MODE_START, 0x40U) == FALSE) goto publish;
    if(wait_data_ready() != FALSE){
        if(read_register(VL53L1X_REG_RANGE_STATUS, &raw_status) != FALSE
                && read_word(VL53L1X_REG_DISTANCE_MM, &raw_mm) != FALSE){
            raw_status &= 0x1FU;
            read_ok = TRUE;
        }
        (void)write_register(VL53L1X_REG_INTERRUPT_CLEAR, 0x01U);
    }
    (void)write_register(VL53L1X_REG_MODE_START, 0x00U);
    /* ST ULDではレジスタ値9がAPIのRangeStatus=0（有効）へ変換される。 */
    if(read_ok == TRUE && raw_status == VL53L1X_RAW_RANGE_VALID){
        *distance_mm = raw_mm;
        result = TRUE;
    }
publish:
    DI(intsts);
    diagnostic_distance = raw_mm;
    diagnostic_status = raw_status;
    diagnostic_ready = read_ok;
    EI(intsts);
    return result;
}
