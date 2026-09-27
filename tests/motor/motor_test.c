#include <assert.h>
#include <stdio.h>
#include "trykernel.h"
static UH levels[8];
void gpio_init_out(unsigned int pin) { (void)pin; }
void gpio_clear(unsigned int pin) { (void)pin; }
void gpio_put(unsigned int pin, int value) { (void)pin; (void)value; }
void pwm_init(void) {}
void pwm_init_slice_b(unsigned int slice, UH top) { (void)slice; (void)top; }
void pwm_set_level_b(unsigned int slice, UH level) { levels[slice] = level; }
/* 内部状態と実際のPWM書き込みを同時に確認する。 */
#include "../../user/motor.c"
static void ticks(UINT count) { while(count--) motor_ramp_step(); }
int main(void)
{
    motor_init();
    motor_drive_forward(20U);
    ticks(10U);
    assert(levels[4] == 0U && levels[5] == 0U);
    motor_obstacle_update(300U, TRUE);
    motor_drive_forward(37U);
    ticks(1U);
    assert(current[0] == 5 && current[1] == 5);
    ticks(7U);
    assert(current[0] == 37 && levels[4] == duty_to_level(37U));
    motor_drive_stop();
    ticks(1U);
    assert(current[0] == 32);
    ticks(7U);
    assert(levels[4] == 0U && levels[5] == 0U);
    motor_drive_forward(20U);
    ticks(4U);
    motor_drive_reverse(20U);
    ticks(4U);
    assert(current[0] == 0 && current[1] == 0);
    ticks(1U);
    assert(current[0] == -5 && current[1] == -5);
    motor_drive_forward(20U);
    ticks(5U);
    motor_obstacle_update(200U, TRUE);
    assert(levels[4] == 0U && levels[5] == 0U);
    assert(target[0] == 0 && target[1] == 0);
    motor_obstacle_update(300U, TRUE);
    ticks(20U);
    assert(levels[4] == 0U && levels[5] == 0U);
    motor_drive_forward(20U);
    ticks(4U);
    motor_obstacle_update(0U, FALSE);
    assert(levels[4] == 0U && levels[5] == 0U);
    motor_drive_reverse(20U);
    ticks(4U);
    assert(current[0] == -20 && current[1] == -20);
    motor_stop_all();
    ticks(20U);
    assert(levels[4] == 0U && levels[5] == 0U);
    puts("motor tests passed: ramp, reversal, obstacle stop, failure and no restart");
    return 0;
}
