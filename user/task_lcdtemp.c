#include <trykernel.h>

#include "adt7410.h"
#include "mpu6050.h"
#include "grove_lcd.h"
#include "task_lcdtemp.h"
#include "uart_tx.h"
#include "mini_printf.h"

#define LCDTEMP_UPDATE_PERIOD  1000

static volatile lcd_display_mode_t lcd_display_mode = LCD_MODE_TEMPERATURE;

BOOL task_lcdtemp_set_mode(lcd_display_mode_t mode)
{
    if(mode > LCD_MODE_GYROSCOPE){
        return FALSE;
    }

    lcd_display_mode = mode;
    return TRUE;
}

lcd_display_mode_t task_lcdtemp_get_mode(void)
{
    return lcd_display_mode;
}

static void pad_lcd_line(char *line)
{
    UW pos = 0U;

    while((pos < GROVE_LCD_COLUMNS) && (line[pos] != '\0')){
        pos++;
    }
    while(pos < GROVE_LCD_COLUMNS){
        line[pos++] = ' ';
    }
    line[GROVE_LCD_COLUMNS] = '\0';
}

static void format_short_milli(INT value, char *text, UW text_size)
{
    UINT magnitude;
    UINT hundredths;
    const char *sign;

    if(value < 0){
        sign = "-";
        magnitude = (UINT)(-value);
    }else{
        sign = "";
        magnitude = (UINT)value;
    }

    hundredths = (magnitude % 1000U) / 10U;
    if(hundredths < 10U){
        mini_snprintf(
            text,
            text_size,
            "%s%u.0%u",
            sign,
            magnitude / 1000U,
            hundredths
        );
    }else{
        mini_snprintf(
            text,
            text_size,
            "%s%u.%u",
            sign,
            magnitude / 1000U,
            hundredths
        );
    }
}

static BOOL read_mpu_values(
    INT *accel_x,
    INT *accel_y,
    INT *accel_z,
    INT *gyro_x,
    INT *gyro_y,
    INT *gyro_z
)
{
    mpu6050_raw_data_t raw_data;

    if(mpu6050_init() == FALSE
            || mpu6050_read_raw(&raw_data) == FALSE){
        return FALSE;
    }

    *accel_x = (raw_data.accel_x * 1000) / 16384;
    *accel_y = (raw_data.accel_y * 1000) / 16384;
    *accel_z = (raw_data.accel_z * 1000) / 16384;
    if(mpu6050_get_gyro_milli_dps(
            &raw_data,
            gyro_x,
            gyro_y,
            gyro_z) == FALSE){
        return FALSE;
    }

    return TRUE;
}

static void format_motion_lines(
    lcd_display_mode_t mode,
    char *first_line,
    char *second_line
)
{
    INT accel_x;
    INT accel_y;
    INT accel_z;
    INT gyro_x;
    INT gyro_y;
    INT gyro_z;
    char x_text[12];
    char y_text[12];
    char z_text[12];

    if(read_mpu_values(
            &accel_x, &accel_y, &accel_z,
            &gyro_x, &gyro_y, &gyro_z) == FALSE){
        mini_snprintf(first_line, GROVE_LCD_COLUMNS + 1U, "MPU read error");
        mini_snprintf(second_line, GROVE_LCD_COLUMNS + 1U, "check sensor");
        return;
    }

    if(mode == LCD_MODE_ACCELERATION){
        format_short_milli(accel_x, x_text, sizeof(x_text));
        format_short_milli(accel_y, y_text, sizeof(y_text));
        format_short_milli(accel_z, z_text, sizeof(z_text));
        mini_snprintf(
            first_line,
            GROVE_LCD_COLUMNS + 1U,
            "AX%s AY%s",
            x_text,
            y_text
        );
        mini_snprintf(
            second_line,
            GROVE_LCD_COLUMNS + 1U,
            "AZ%s g",
            z_text
        );
    }else{
        format_short_milli(gyro_x, x_text, sizeof(x_text));
        format_short_milli(gyro_y, y_text, sizeof(y_text));
        format_short_milli(gyro_z, z_text, sizeof(z_text));
        mini_snprintf(
            first_line,
            GROVE_LCD_COLUMNS + 1U,
            "GX%s GY%s",
            x_text,
            y_text
        );
        mini_snprintf(
            second_line,
            GROVE_LCD_COLUMNS + 1U,
            "GZ%s dps",
            z_text
        );
    }
}

void format_temperature_line(INT temperature_milli_c, char *line)
{
    UINT magnitude;
    UINT integer_part;
    UINT fractional_part;
    UINT divisor;
    UW pos = 0U;

    for(UW i = 0U; i < GROVE_LCD_COLUMNS; i++){
        line[i] = ' ';
    }
    line[GROVE_LCD_COLUMNS] = '\0';

    if(temperature_milli_c < 0){
        line[pos++] = '-';
        magnitude = (UINT)(-temperature_milli_c);
    }else{
        magnitude = (UINT)temperature_milli_c;
    }

    integer_part = magnitude / 1000U;
    fractional_part = magnitude % 1000U;

    divisor = 1U;
    while((integer_part / divisor) >= 10U){
        divisor *= 10U;
    }

    do{
        line[pos++] = (char)('0' + (integer_part / divisor));
        integer_part %= divisor;
        divisor /= 10U;
    }while(divisor != 0U);

    line[pos++] = '.';
    line[pos++] = (char)('0' + (fractional_part / 100U));
    line[pos++] = (char)('0' + ((fractional_part / 10U) % 10U));
    line[pos++] = (char)('0' + (fractional_part % 10U));
    line[pos++] = ' ';
    line[pos] = 'C';
}

void task_lcdtemp(INT stacd, void *exinf)
{
    INT temperature_milli_c;
    char first_line[GROVE_LCD_COLUMNS + 1U];
    char second_line[GROVE_LCD_COLUMNS + 1U];
    BOOL initialized = FALSE;
    BOOL write_error_reported = FALSE;

    (void)stacd;
    (void)exinf;

    while(TRUE){
        if(initialized == FALSE){
            if(grove_lcd_init() == FALSE){
                uart_tx_send("LCD temperature task initialization error\r\n");
                tk_dly_tsk(LCDTEMP_UPDATE_PERIOD);
                continue;
            }
            initialized = TRUE;
        }

        if(lcd_display_mode == LCD_MODE_TEMPERATURE){
            mini_snprintf(
                first_line,
                sizeof(first_line),
                "ADT7410 Temp"
            );

            if(adt7410_read_temperature(&temperature_milli_c) == FALSE){
                mini_snprintf(
                    second_line,
                    sizeof(second_line),
                    "sensor error"
                );
            }else{
                format_temperature_line(temperature_milli_c, second_line);
            }
        }else{
            format_motion_lines(lcd_display_mode, first_line, second_line);
        }

        pad_lcd_line(first_line);
        pad_lcd_line(second_line);

        if(grove_lcd_update_lines(first_line, second_line) == FALSE){
            if(write_error_reported == FALSE){
                uart_tx_send("LCD sensor task write error\r\n");
                write_error_reported = TRUE;
            }
        }else{
            write_error_reported = FALSE;
        }

        tk_dly_tsk(LCDTEMP_UPDATE_PERIOD);
    }
}
