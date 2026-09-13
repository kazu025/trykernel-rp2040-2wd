#ifndef TASK_INAPOWER_H
#define TASK_INAPOWER_H

#include <trykernel.h>

typedef struct {
    UW sample_count;
    UW bus_voltage_mv;
    INT current_ua;
    INT power_mw;
} INA226_MEASUREMENT;

void task_inapower_init(void);
BOOL task_inapower_get(INA226_MEASUREMENT *measurement, UW *error_count);
void task_inapower(INT stacd, void *exinf);

#endif /* TASK_INAPOWER_H */
