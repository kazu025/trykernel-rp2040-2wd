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
#define MOTOR_STOP_DISTANCE_MM   200U

/* 起動直後や測距失敗時は前進を許可しない。 */
static volatile BOOL obstacle_blocked = TRUE;
static INT current[2];
static INT target[2];

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
    if(obstacle_blocked == TRUE && forward == TRUE) duty_percent = 0U;
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

BOOL motor_obstacle_blocked(void)
{
    return obstacle_blocked;
}

/* 呼び出し元は割り込み禁止中。 */
static void apply_wheel(UINT wheel, INT duty)
{
    motor_set(wheel == 0U ? MOTOR_LEFT_PHASE_PIN : MOTOR_RIGHT_PHASE_PIN,
              wheel == 0U ? MOTOR_LEFT_PWM_SLICE : MOTOR_RIGHT_PWM_SLICE,
              duty >= 0, (UB)(duty < 0 ? -duty : duty));
}

static void stop_locked(void)
{
    current[0] = current[1] = target[0] = target[1] = 0;
    apply_wheel(0U, 0);
    apply_wheel(1U, 0);
}

void motor_obstacle_update(UH distance_mm, BOOL valid)
{
    UINT intsts;
    DI(intsts);
    if(valid == FALSE || distance_mm <= MOTOR_STOP_DISTANCE_MM){
        if(obstacle_blocked == FALSE) stop_locked();
        obstacle_blocked = TRUE;
    }else{
        obstacle_blocked = FALSE;
    }
    EI(intsts);
}

static void set_immediate(UINT wheel, BOOL forward, UB duty)
{
    UINT intsts;
    DI(intsts);
    if(duty > 100U) duty = 100U;
    if(obstacle_blocked == TRUE && forward == TRUE) duty = 0U;
    current[wheel] = target[wheel] = forward ? (INT)duty : -(INT)duty;
    apply_wheel(wheel, current[wheel]);
    EI(intsts);
}

void motor_left_set(BOOL forward, UB duty) { set_immediate(0U, forward, duty); }
void motor_right_set(BOOL forward, UB duty) { set_immediate(1U, forward, duty); }
void motor_left_stop(void) { set_immediate(0U, TRUE, 0U); }
void motor_right_stop(void) { set_immediate(1U, TRUE, 0U); }

void motor_stop_all(void)
{
    UINT intsts;
    DI(intsts);
    stop_locked();
    EI(intsts);
}

static void set_targets(INT left, INT right)
{
    UINT intsts;
    DI(intsts);
    if(obstacle_blocked == FALSE || (left <= 0 && right <= 0)){
        target[0] = left;
        target[1] = right;
    }
    EI(intsts);
}

/* 50msごとに5ポイント更新。逆転時は0を経由する。 */
void motor_ramp_step(void)
{
    UINT intsts, wheel;
    DI(intsts);
    for(wheel = 0U; wheel < 2U; wheel++){
        INT next = current[wheel];
        INT goal = target[wheel];
        if((next > 0 && goal < 0) || (next < 0 && goal > 0)) goal = 0;
        if(next < goal){
            next += 5;
            if(next > goal) next = goal;
        }else if(next > goal){
            next -= 5;
            if(next < goal) next = goal;
        }
        if(next != current[wheel]){
            current[wheel] = next;
            apply_wheel(wheel, next);
        }
    }
    EI(intsts);
}

void motor_drive_stop(void) { set_targets(0, 0); }
void motor_drive_forward(UB duty) { set_targets(duty, duty); }
void motor_drive_reverse(UB duty) { set_targets(-(INT)duty, -(INT)duty); }
void motor_drive_left(UB duty) { set_targets(-(INT)duty, duty); }
void motor_drive_right(UB duty) { set_targets(duty, -(INT)duty); }
