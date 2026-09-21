#ifndef MOTOR_H
#define MOTOR_H

#include "typedef.h"

void motor_init(void);
void motor_left_set(BOOL forward, UB duty_percent);
void motor_right_set(BOOL forward, UB duty_percent);
void motor_left_stop(void);
void motor_right_stop(void);
void motor_stop_all(void);
void motor_drive_forward(UB duty_percent);
void motor_drive_reverse(UB duty_percent);
void motor_drive_left(UB duty_percent);
void motor_drive_right(UB duty_percent);

#endif /* MOTOR_H */
