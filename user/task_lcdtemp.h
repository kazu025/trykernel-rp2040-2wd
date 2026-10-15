/**
 * @file    task_lcdtemp.h
 * @brief   LCDセンサー表示タスクのモードとAPIを宣言する。
 *
 * @details 温度、加速度、角速度の表示モードを定義し、LCD表示タスクの
 *          起動、表示モード設定・取得、および温度文字列変換のAPIを公開する。
 */

#ifndef TASK_LCDTEMP_H
#define TASK_LCDTEMP_H

#include <trykernel.h>

/**
 * @brief LCDに表示するセンサー情報の種類。
 */
typedef enum {
    LCD_MODE_TEMPERATURE = 0, //!< ADT7410の温度を表示する。
    LCD_MODE_ACCELERATION,    //!< MPU6050の3軸加速度を表示する。
    LCD_MODE_GYROSCOPE        //!< MPU6050の3軸角速度を表示する。
} lcd_display_mode_t;

void task_lcdtemp(INT stacd, void *exinf);
BOOL task_lcdtemp_set_mode(lcd_display_mode_t mode);
lcd_display_mode_t task_lcdtemp_get_mode(void);
void format_temperature_line(INT temperature_milli_c, char *line);

#endif /* TASK_LCDTEMP_H */
