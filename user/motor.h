#ifndef MOTOR_H
#define MOTOR_H

#include "typedef.h"

void motor_init(void);
void motor_left_set(BOOL forward, UB duty_percent);
void motor_right_set(BOOL forward, UB duty_percent);
void motor_left_stop(void);
void motor_right_stop(void);
void motor_stop_all(void);

#endif /* MOTOR_H */
