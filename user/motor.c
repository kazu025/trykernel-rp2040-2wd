#include <trykernel.h>
#include "gpio.h"
#include "pwm.h"
#include "motor.h"

/* DRV8835-S: MODEは3.3Vへ接続し、PHASE/ENABLEモードで使用する。 */
#define MOTOR_LEFT_PHASE_PIN    8U
#define MOTOR_LEFT_ENABLE_PIN   9U
#define MOTOR_RIGHT_PHASE_PIN   10U
#define MOTOR_RIGHT_ENABLE_PIN  11U

/* GPIO9はPWM slice 4B、GPIO11はPWM slice 5B。125MHzで20kHz PWM。 */
#define MOTOR_LEFT_PWM_SLICE    4U
#define MOTOR_RIGHT_PWM_SLICE   5U
#define MOTOR_PWM_TOP           6249U

static UH duty_to_level(UB duty_percent)
{
    UW level;

    if(duty_percent > 100U) duty_percent = 100U;
    level = ((UW)MOTOR_PWM_TOP * (UW)duty_percent) / 100U;
    return (UH)level;
}

static void motor_set(unsigned int phase_pin, unsigned int pwm_slice,
                      BOOL forward, UB duty_percent)
{
    /* 方向を変える前に必ずPWMを0%にして、Hブリッジを停止する。 */
    pwm_set_level_b(pwm_slice, 0U);
    gpio_put(phase_pin, (forward == TRUE) ? 0 : 1);
    pwm_set_level_b(pwm_slice, duty_to_level(duty_percent));
}

void motor_init(void)
{
    /* PWM出力を有効にする前から、方向ピンをLowにして停止させる。 */
    gpio_init_out(MOTOR_LEFT_PHASE_PIN);
    gpio_init_out(MOTOR_RIGHT_PHASE_PIN);
    gpio_clear(MOTOR_LEFT_PHASE_PIN);
    gpio_clear(MOTOR_RIGHT_PHASE_PIN);
    pwm_init();
    pwm_init_slice_b(MOTOR_LEFT_PWM_SLICE, MOTOR_PWM_TOP);
    pwm_init_slice_b(MOTOR_RIGHT_PWM_SLICE, MOTOR_PWM_TOP);
    out_w(GPIO_CTRL(MOTOR_LEFT_ENABLE_PIN), GPIO_CTRL_FUNCSEL_PWM);
    out_w(GPIO_CTRL(MOTOR_RIGHT_ENABLE_PIN), GPIO_CTRL_FUNCSEL_PWM);
    motor_stop_all();
}

void motor_left_set(BOOL forward, UB duty_percent)
{
    motor_set(MOTOR_LEFT_PHASE_PIN, MOTOR_LEFT_PWM_SLICE, forward, duty_percent);
}

void motor_right_set(BOOL forward, UB duty_percent)
{
    motor_set(MOTOR_RIGHT_PHASE_PIN, MOTOR_RIGHT_PWM_SLICE, forward, duty_percent);
}

void motor_left_stop(void)
{
    pwm_set_level_b(MOTOR_LEFT_PWM_SLICE, 0U);
    gpio_clear(MOTOR_LEFT_PHASE_PIN);
}

void motor_right_stop(void)
{
    pwm_set_level_b(MOTOR_RIGHT_PWM_SLICE, 0U);
    gpio_clear(MOTOR_RIGHT_PHASE_PIN);
}

void motor_stop_all(void)
{
    motor_left_stop();
    motor_right_stop();
}

void motor_drive_forward(UB duty_percent)
{
    motor_left_set(TRUE, duty_percent);
    motor_right_set(TRUE, duty_percent);
}

void motor_drive_reverse(UB duty_percent)
{
    motor_left_set(FALSE, duty_percent);
    motor_right_set(FALSE, duty_percent);
}

void motor_drive_left(UB duty_percent)
{
    motor_left_set(FALSE, duty_percent);
    motor_right_set(TRUE, duty_percent);
}

void motor_drive_right(UB duty_percent)
{
    motor_left_set(TRUE, duty_percent);
    motor_right_set(FALSE, duty_percent);
}
