/**
 * @file    motor.c
 * @brief   左右モーターのPWM駆動と車体の走行制御を行う。
 *
 * @details
 * DRV8835-SをPHASE/ENABLEモードで制御し、左右モーターの回転方向と
 * PWMデューティ比を設定する。個別モーターの即時操作に加え、車体の
 * 走行指令を目標値として保持し、定期的なランプ処理で滑らかに反映する。
 * 距離センサーの状態に基づく障害物停止制御を備え、距離が閾値以下の
 * 場合や測距結果が無効な場合は前進を禁止する。
 */

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

/**
 * @brief PWMデューティ比をPWMレベルへ変換する。
 * @param[in] duty_percent デューティ比（百分率）。100を超える値は100として扱う。
 * @return 対応するPWM比較レベル。
 */
static UH duty_to_level(UB duty_percent)
{
    UW level;

    if(duty_percent > 100U) duty_percent = 100U;
    level = ((UW)MOTOR_PWM_TOP * (UW)duty_percent) / 100U;
    return (UH)level;
}

/**
 * @brief 指定したモーターの方向ピンとPWM出力を設定する。
 * @param[in] phase_pin 回転方向を設定するGPIOピン番号。
 * @param[in] pwm_slice PWM出力に使用するスライス番号。
 * @param[in] forward TRUEは前進方向、FALSEは逆転方向。
 * @param[in] duty_percent PWMデューティ比（百分率）。
 * @details 障害物が検出されている間は前進側のデューティ比を0にする。
 *          方向切り替え時はいったんPWMを停止してから方向と出力を設定する。
 */
static void motor_set(unsigned int phase_pin, unsigned int pwm_slice,
                      BOOL forward, UB duty_percent)
{
    if(obstacle_blocked == TRUE && forward == TRUE) duty_percent = 0U;
    /* 方向を変える前に必ずPWMを0%にして、Hブリッジを停止する。 */
    pwm_set_level_b(pwm_slice, 0U);
    gpio_put(phase_pin, (forward == TRUE) ? 0 : 1);
    pwm_set_level_b(pwm_slice, duty_to_level(duty_percent));
}

/**
 * @brief モーター制御用GPIOとPWMを初期化し、全モーターを停止する。
 */
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

/**
 * @brief 障害物または無効な測距値による走行禁止状態を取得する。
 * @retval TRUE 前進が禁止されている。
 * @retval FALSE 前進が許可されている。
 */
BOOL motor_obstacle_blocked(void)
{
    return obstacle_blocked;
}

/**
 * @brief 指定した車輪へ符号付きデューティ比を適用する。
 * @param[in] wheel 車輪番号（0は左、1は右）。
 * @param[in] duty 符号付きデューティ比。正は前進、負は逆転、0は停止。
 * @details 呼び出し元が割り込みを禁止している状態で使用する。
 */
static void apply_wheel(UINT wheel, INT duty)
{
    motor_set(wheel == 0U ? MOTOR_LEFT_PHASE_PIN : MOTOR_RIGHT_PHASE_PIN,
              wheel == 0U ? MOTOR_LEFT_PWM_SLICE : MOTOR_RIGHT_PWM_SLICE,
              duty >= 0, (UB)(duty < 0 ? -duty : duty));
}

/**
 * @brief 左右車輪の現在値と目標値を0にし、両輪を停止する。
 * @details 呼び出し元が割り込みを禁止している状態で使用する。
 */
static void stop_locked(void)
{
    current[0] = current[1] = target[0] = target[1] = 0;
    apply_wheel(0U, 0);
    apply_wheel(1U, 0);
}

/**
 * @brief 距離測定結果から障害物による走行禁止状態を更新する。
 * @param[in] distance_mm 測定距離（ミリメートル）。
 * @param[in] valid 測定値が有効な場合はTRUE。
 * @details 測定値が無効または停止距離以下の場合は前進を禁止し、
 *          走行中に禁止へ移行したときは左右のモーターを停止する。
 */
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

/**
 * @brief 指定した車輪の駆動値を即時に設定する。
 * @param[in] wheel 車輪番号（0は左、1は右）。
 * @param[in] forward TRUEは前進方向、FALSEは逆転方向。
 * @param[in] duty PWMデューティ比（百分率）。100を超える値は100として扱う。
 * @details 現在値と目標値を同じ値に設定する。障害物検出中の前進指令は停止扱いにする。
 */
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

/**
 * @brief 左モーターの回転方向とデューティ比を即時に設定する。
 * @param[in] forward TRUEは前進方向、FALSEは逆転方向。
 * @param[in] duty PWMデューティ比（百分率）。
 */
void motor_left_set(BOOL forward, UB duty) { set_immediate(0U, forward, duty); }

/**
 * @brief 右モーターの回転方向とデューティ比を即時に設定する。
 * @param[in] forward TRUEは前進方向、FALSEは逆転方向。
 * @param[in] duty PWMデューティ比（百分率）。
 */
void motor_right_set(BOOL forward, UB duty) { set_immediate(1U, forward, duty); }

/**
 * @brief 左モーターを即時停止する。
 */
void motor_left_stop(void) { set_immediate(0U, TRUE, 0U); }

/**
 * @brief 右モーターを即時停止する。
 */
void motor_right_stop(void) { set_immediate(1U, TRUE, 0U); }

/**
 * @brief 左右モーターを即時停止し、現在値と目標値をクリアする。
 */
void motor_stop_all(void)
{
    UINT intsts;
    DI(intsts);
    stop_locked();
    EI(intsts);
}

/**
 * @brief 左右車輪の目標デューティ比を設定する。
 * @param[in] left 左車輪の符号付き目標値。正は前進、負は逆転。
 * @param[in] right 右車輪の符号付き目標値。正は前進、負は逆転。
 * @details 障害物検出中は、左右とも逆転または停止となる目標値のみ設定する。
 */
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

/**
 * @brief 左右モーターの現在値を目標値へ向けて1段階更新する。
 * @details タスクから定期的に呼び出し、各呼び出しでデューティ比を最大5ポイント
 *          変化させる。回転方向を反転するときは、いったん0を経由する。
 */
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

/**
 * @brief 車体の左右車輪に停止目標を設定する。
 */
void motor_drive_stop(void) { set_targets(0, 0); }

/**
 * @brief 車体に前進目標を設定する。
 * @param[in] duty PWMデューティ比（百分率）。
 */
void motor_drive_forward(UB duty) { set_targets(duty, duty); }

/**
 * @brief 車体に後退目標を設定する。
 * @param[in] duty PWMデューティ比（百分率）。
 */
void motor_drive_reverse(UB duty) { set_targets(-(INT)duty, -(INT)duty); }

/**
 * @brief 車体に左旋回目標を設定する（左輪を後退、右輪を前進）。
 * @param[in] duty PWMデューティ比（百分率）。
 */
void motor_drive_left(UB duty) { set_targets(-(INT)duty, duty); }

/**
 * @brief 車体に右旋回目標を設定する（左輪を前進、右輪を後退）。
 * @param[in] duty PWMデューティ比（百分率）。
 */
void motor_drive_right(UB duty) { set_targets(duty, -(INT)duty); }
