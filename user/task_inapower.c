/**
 * @file    task_inapower.c
 * @brief   INA226の電圧・電流・電力を定期測定して共有する。
 *
 * @details
 * INA226からバス電圧とシャント電流を読み取り、電力を算出する。
 * 最新の測定値、サンプル数、測定エラー数を共有し、取得APIを通じて
 * 他のタスクから参照できるようにする。
 */

#include <trykernel.h>
#include "ina226.h"
#include "task_inapower.h"
#include "uart_tx.h"

#define INAPOWER_MEASUREMENT_PERIOD  1000 //!< INA226測定タスクの測定周期（ミリ秒）。

static volatile BOOL inapower_ready;             //!< 有効な測定値を取得済みの場合にTRUE。
static volatile UW inapower_sample_count;        //!< 成功した測定の累積回数。
static volatile UW inapower_bus_voltage_mv;      //!< 最新のバス電圧（mV）。
static volatile INT inapower_current_ua;          //!< 最新の電流（µA）。
static volatile INT inapower_power_mw;            //!< 最新の電力（mW）。
static volatile UW inapower_error_count;          //!< 電圧または電流の測定に失敗した回数。

/**
 * @brief INA226測定値と状態カウンタを初期化する。
 * @details 初期化直後は測定値を0とし、有効な測定値がまだない状態にする。
 */
void task_inapower_init(void)
{
    inapower_ready = FALSE;
    inapower_sample_count = 0U;
    inapower_bus_voltage_mv = 0U;
    inapower_current_ua = 0;
    inapower_power_mw = 0;
    inapower_error_count = 0U;
}

/**
 * @brief INA226の最新測定値と測定エラー数を取得する。
 * @param[out] measurement 測定値（サンプル数、バス電圧、電流、電力）の格納先。
 * @param[out] error_count 測定エラー数の格納先。
 * @retval TRUE 有効な測定値を取得済みで、出力引数へ値を格納した。
 * @retval FALSE 引数がNULL、または有効な測定値がまだない。
 * @details 共有データの読み取り中にタスク側の更新が割り込まないよう、
 *          割り込み禁止区間で各値をコピーする。
 */
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

/**
 * @brief INA226を周期測定し、電圧・電流・電力の共有値を更新するタスク。
 * @param[in] stacd タスク起動コード（この関数では使用しない）。
 * @param[in] exinf タスク拡張情報（この関数では使用しない）。
 * @details バス電圧とシャント電流の両方を正常に読み取れた場合に電力を算出し、
 *          測定値と成功回数を更新する。読み取りに失敗した場合はエラー数を加算する。
 *          いずれの場合も測定周期分待機して次の測定を行う。
 */
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
