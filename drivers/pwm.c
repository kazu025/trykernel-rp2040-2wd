#include <trykernel.h>
#include "pwm.h"

#define PWM_SLICE_COUNT 8U

void pwm_init(void)
{
    UW reset_mask = RESETS_RESET_PWM;

    out_w(RESETS_RESET, in_w(RESETS_RESET) | reset_mask);
    out_w(RESETS_RESET, in_w(RESETS_RESET) & ~reset_mask);
    while((in_w(RESETS_RESET_DONE) & reset_mask) == 0U){}
}

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

void pwm_set_level_b(unsigned int slice, UH level)
{
    UW cc;

    if(slice >= PWM_SLICE_COUNT) return;
    cc = in_w(PWM_CC(slice));
    cc &= 0x0000FFFFU;
    out_w(PWM_CC(slice), cc | PWM_CC_B(level));
}
