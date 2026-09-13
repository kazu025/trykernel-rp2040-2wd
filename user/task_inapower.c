#include <trykernel.h>
#include "ina226.h"
#include "task_inapower.h"
#include "uart_tx.h"

#define INAPOWER_MEASUREMENT_PERIOD  1000

static volatile BOOL inapower_ready;
static volatile UW inapower_sample_count;
static volatile UW inapower_bus_voltage_mv;
static volatile INT inapower_current_ua;
static volatile INT inapower_power_mw;
static volatile UW inapower_error_count;

void task_inapower_init(void)
{
    inapower_ready = FALSE;
    inapower_sample_count = 0U;
    inapower_bus_voltage_mv = 0U;
    inapower_current_ua = 0;
    inapower_power_mw = 0;
    inapower_error_count = 0U;
}

BOOL task_inapower_get(INA226_MEASUREMENT *measurement, UW *error_count)
{
    UINT intsts;
    BOOL ready;

    if((measurement == NULL) || (error_count == NULL)) return FALSE;

    DI(intsts);
    ready = inapower_ready;
    measurement->sample_count = inapower_sample_count;
    measurement->bus_voltage_mv = inapower_bus_voltage_mv;
    measurement->current_ua = inapower_current_ua;
    measurement->power_mw = inapower_power_mw;
    *error_count = inapower_error_count;
    EI(intsts);

    return ready;
}

void task_inapower(INT stacd, void *exinf)
{
    UINT bus_raw;
    UW bus_voltage_mv;
    INT shunt_raw;
    INT shunt_voltage_uv;
    INT current_ua;
    INT power_mw;
    UINT intsts;

    (void)stacd;
    (void)exinf;

    uart_tx_send("INA226 periodic measurement task start (1s)\r\n");

    while(TRUE){
        if(ina226_read_bus_voltage(&bus_raw, &bus_voltage_mv) != FALSE
                && ina226_read_shunt(
                    &shunt_raw, &shunt_voltage_uv, &current_ua) != FALSE){
            /* mV * uA / 1,000,000 = mW */
            power_mw =
                (INT)(((D)bus_voltage_mv * (D)current_ua) / 1000000);

            DI(intsts);
            inapower_sample_count++;
            inapower_bus_voltage_mv = bus_voltage_mv;
            inapower_current_ua = current_ua;
            inapower_power_mw = power_mw;
            inapower_ready = TRUE;
            EI(intsts);
        }else{
            inapower_error_count++;
        }

        (void)tk_dly_tsk(INAPOWER_MEASUREMENT_PERIOD);
    }
}
