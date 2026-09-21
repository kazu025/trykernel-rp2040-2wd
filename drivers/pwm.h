#ifndef RP2040_PWM_H
#define RP2040_PWM_H

#include "typedef.h"

void pwm_init(void);
void pwm_init_slice_b(unsigned int slice, UH top);
void pwm_set_level_b(unsigned int slice, UH level);

#endif /* RP2040_PWM_H */
