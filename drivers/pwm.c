/**
 * @file pwm.c
 * @brief RP2040のPWMスライスBチャネルを制御するドライバ。
 *
 * PWM周辺回路のリセット、スライスの初期設定、および
 * Bチャネルの比較値（出力レベル）の更新を行う。
 * GPIOのPWM機能への切り替えは、このファイルでは行わない。
 * @note
 * RP2040には 8スライス × A・Bの2チャネル = 計16チャネル のPWM出力がある。
 * 同じスライスのAとBは周期を共有します。
 * 出力の割合（デューティ比）は、それぞれ個別に設定できます
 * 本ソースはBチャンネルのみ使用
 */
#include <trykernel.h>
#include "pwm.h"

/**
 * @brief RP2040のPWMスライス数。
 *
 * 有効なスライス番号は0～7。指定された番号の範囲確認に使用する。
 */
#define PWM_SLICE_COUNT 8U

/**
 * @brief PWM周辺回路をリセットして使用可能な状態にする。
 *
 * PWMのリセットを有効にした後で解除し、リセット完了を示す
 * RESETS_RESET_DONEレジスタのビットが立つまで待つ。
 *
 * @note リセット完了待ちにタイムアウトはない。
 */
void pwm_init(void)
{
    UW reset_mask = RESETS_RESET_PWM;

    out_w(RESETS_RESET, in_w(RESETS_RESET) | reset_mask);
    out_w(RESETS_RESET, in_w(RESETS_RESET) & ~reset_mask);
    while((in_w(RESETS_RESET_DONE) & reset_mask) == 0U){}
}
/**
 * @brief 指定したPWMスライスのBチャネルを初期化する。
 *
 * スライスを停止してから、クロック分周比を1、カウンタを0、
 * カウンタの上限値をtop、Bチャネルの比較値を0に設定する。
 * 設定後、スライスを有効にする。
 *
 * @param slice PWMスライス番号（0～7）。範囲外なら何もせずに戻る。
 * @param top カウンタの上限値。
 */
void pwm_init_slice_b(unsigned int slice, UH top)
{
    UW enable;

    if(slice >= PWM_SLICE_COUNT) return;
    out_w(PWM_CSR(slice), 0U);
    out_w(PWM_DIV(slice), PWM_DIV_INT(1U));
    out_w(PWM_CTR(slice), 0U);
    out_w(PWM_TOP(slice), (UW)top);
    out_w(PWM_CC(slice), PWM_CC_B(0U));
    out_w(PWM_CSR(slice), PWM_CSR_EN);
    enable = in_w(PWM_EN);
    out_w(PWM_EN, enable | (1U << slice));
}
/**
 * @brief 指定したPWMスライスのBチャネルの比較値を設定する。
 *
 * 比較値レジスタのAチャネル側を保持したまま、Bチャネル側の値を
 * levelに更新する。比較値はPWM出力のデューティ比を決める。
 *
 * @param slice PWMスライス番号（0～7）。範囲外なら何もせずに戻る。
 * @param level Bチャネルに設定する比較値。
 * @note levelがTOP以下かどうかは、この関数では確認しない。
 * デューティー比=level/(top+1) x 100
 */
void pwm_set_level_b(unsigned int slice, UH level)
{
    UW cc;

    if(slice >= PWM_SLICE_COUNT) return;
    cc = in_w(PWM_CC(slice));
    cc &= 0x0000FFFFU;
    out_w(PWM_CC(slice), cc | PWM_CC_B(level));
}
