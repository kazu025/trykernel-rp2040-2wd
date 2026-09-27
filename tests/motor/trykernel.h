#ifndef MOTOR_TEST_TRYKERNEL_H
#define MOTOR_TEST_TRYKERNEL_H
#ifdef NULL
#undef NULL
#endif
#include "typedef.h"
#define DI(state) ((state) = 0U)
#define EI(state) ((void)(state))
#define GPIO_CTRL(pin) (pin)
#define GPIO_CTRL_FUNCSEL_PWM 4U
static inline void out_w(UINT address, UINT value) { (void)address; (void)value; }
#endif
