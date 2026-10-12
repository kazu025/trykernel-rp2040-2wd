#ifndef TASK_LCDTEMP_H
#define TASK_LCDTEMP_H

#include <trykernel.h>

typedef enum {
    LCD_MODE_TEMPERATURE = 0,
    LCD_MODE_ACCELERATION,
    LCD_MODE_GYROSCOPE
} lcd_display_mode_t;

void task_lcdtemp(INT stacd, void *exinf);
BOOL task_lcdtemp_set_mode(lcd_display_mode_t mode);
lcd_display_mode_t task_lcdtemp_get_mode(void);
void format_temperature_line(INT temperature_milli_c, char *line);

#endif /* TASK_LCDTEMP_H */
