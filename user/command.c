#include <trykernel.h>
#include "task_led.h"
#include "command.h"
#include "uart_tx.h"
#include "mini_printf.h"
#include "uart.h"
#include "gpio.h"
#include "i2c.h"
#include "adt7410.h"
#include "ina226.h"
#include "mpu6050.h"
#include "grove_lcd.h"
#include "task_lcdtemp.h"
#include "task_mpuirq.h"
#include "task_msgtest.h"
#include "task_motionled.h"
#include "w25qxx.h"
#include "task_flashlog.h"
#include "task_inapower.h"

/* --- コマンドバッファ最大数 --- */
#define CMD_MAX_ARGS    16
#define CMD_OUTPUT_BUF_SIZE 128U
static void cmd_help(int argc, char *argv[]);
static void cmd_status(int argc, char *argv[]);
static void cmd_echo(int argc, char *argv[]);
static void cmd_led(int argc, char *argv[]);
static void cmd_print(int argc, char *argv[]);
static int split_args(char *line, char *argv[], int max_args);
static void cmd_i2cscan(int argc, char *argv[]);
static void cmd_temperature(int argc, char *argv[]);
static void cmd_adtinfo(int argc, char *argv[]);
static void cmd_adtconfig(int argc, char *argv[]);
static void cmd_adtraw(int argc, char *argv[]);
static void cmd_inaid(int argc, char *argv[]);
static void cmd_inabus(int argc, char *argv[]);
static void cmd_inashunt(int argc, char *argv[]);
static void cmd_inapower(int argc, char *argv[]);
static void cmd_mpuid(int argc, char *argv[]);
static void cmd_mpuraw(int argc, char *argv[]);
static void cmd_mpu(int argc, char *argv[]);
static void cmd_mpucal(int argc, char *argv[]);
static void cmd_mpuirq(int argc, char *argv[]);
static void cmd_motion(int argc, char *argv[]);
static void cmd_msgsend(int argc, char *argv[]);
static void cmd_msgtest(int argc, char *argv[]);
static void cmd_flashid(int argc, char *argv[]);
static void cmd_flashstatus(int argc, char *argv[]);
static void cmd_flashwen(int argc, char *argv[]);
static void cmd_flasherase(int argc, char *argv[]);
static void cmd_flashwrite(int argc, char *argv[]);
static void cmd_flashread(int argc, char *argv[]);
static void cmd_flashlog(int argc, char *argv[]);
static void cmd_flashlogclear(int argc, char *argv[]);
static void cmd_lcdtest(int argc, char *argv[]);
static void cmd_lcdtemp(int argc, char *argv[]);
static void cmd_lcdcolor(int argc, char *argv[]);
static void cmd_lcdmode(int argc, char *argv[]);
static void format_temperature_line(INT temperature_milli_c, char *line);
static BOOL parse_u8(const char *text, UB *value);
static BOOL str_eq(const char *a, const char *b);
typedef void (*cmd_func_t)(int argc, char *argv[]);
typedef struct {
    const char *name;
    cmd_func_t func;
    const char *help;
} command_t;
static const command_t command_table[] = {
    {"help",    cmd_help,   "show command list"},
    {"h",    cmd_help,   "show command list"},
    {"status",  cmd_status, "show system status"},
    {"echo",    cmd_echo,  "echo arguments"},
    {"led",     cmd_led,    "led on/off/blink"},
    {"print",   cmd_print,  "print test"},
    {"i2cscan", cmd_i2cscan, "scan I2C devices"},
    {"temperature", cmd_temperature, "read temperature from ADT7410 sensor"},
    {"adtinfo", cmd_adtinfo, "show ADT7410 ID and configuration"},
    {"adtconfig", cmd_adtconfig, "set ADT7410 resolution: 13|16"},
    {"adtraw", cmd_adtraw, "show ADT7410 raw temperature data"},
    {"inaid", cmd_inaid, "show INA226 manufacturer and die IDs"},
    {"inabus", cmd_inabus, "show INA226 bus voltage"},
    {"inashunt", cmd_inashunt, "show INA226 shunt voltage and current"},
    {"inapower", cmd_inapower, "show INA226 voltage, current and power"},
    {"mpuid", cmd_mpuid, "show MPU-6050 WHO_AM_I"},
    {"mpuraw", cmd_mpuraw, "show MPU-6050 raw sensor data"},
    {"mpu", cmd_mpu, "show acceleration, gyro and temperature"},
    {"mpucal", cmd_mpucal, "calibrate MPU gyro while stationary"},
    {"mpuirq", cmd_mpuirq, "show MPU data ready IRQ status"},
    {"motion", cmd_motion, "start motion measurement after 3-second settling"},
    {"msgsend", cmd_msgsend, "send a test message to another task"},
    {"msgtest", cmd_msgtest, "test message FIFO, full queue and timeout"},
    {"flashid", cmd_flashid, "show SPI flash JEDEC ID"},
    {"flashstatus", cmd_flashstatus, "show SPI flash status register-1"},
    {"flashwen", cmd_flashwen, "test SPI flash Write Enable latch"},
    {"flasherase", cmd_flasherase, "erase last test sector: confirm"},
    {"flashwrite", cmd_flashwrite, "write and verify last test sector: confirm"},
    {"flashread", cmd_flashread, "read first 32 bytes of last test sector"},
    {"flashlog", cmd_flashlog, "show motion history stored in SPI flash"},
    {"flashlogclear", cmd_flashlogclear, "erase motion history: confirm"},
    {"lcdtest", cmd_lcdtest, "test Grove RGB LCD V5.0"},
    {"lcdtemp", cmd_lcdtemp, "show ADT7410 temperature on LCD"},
    {"lcdcolor", cmd_lcdcolor, "set LCD backlight: R G B"},
    {"lcdmode", cmd_lcdmode, "set LCD mode: temp|accel|gyro"}
};
static const int command_count = sizeof(command_table)/sizeof(command_table[0]);

/* 受信した行を処理する */
void command_execute(char *line){
    char *argv[CMD_MAX_ARGS];
    int argc;
    argc = split_args(line, argv, CMD_MAX_ARGS);
    if(argc == 0) return;
    for(int i=0; i<command_count; i++){
        if(str_eq(argv[0], command_table[i].name) == TRUE){
            command_table[i].func(argc, argv);
            return;
        }
    }
    uart_tx_printf("unknown command: %s\r\n", argv[0]);
}
/*
 * 文字列比較
 */
static BOOL str_eq(const char *a, const char *b)
{
    while ((*a != '\0') && (*b != '\0')) {
        if (*a != *b) {
            return FALSE;
        }

        a++;
        b++;
    }

    return ((*a == '\0') && (*b == '\0'));
}

static BOOL parse_u8(const char *text, UB *value)
{
    UINT number = 0U;

    if((text == NULL) || (value == NULL) || (*text == '\0')){
        return FALSE;
    }

    while(*text != '\0'){
        UINT digit;

        if((*text < '0') || (*text > '9')){
            return FALSE;
        }

        digit = (UINT)(*text - '0');
        if((number > 25U) || ((number == 25U) && (digit > 5U))){
            return FALSE;
        }

        number = (number * 10U) + digit;
        text++;
    }

    *value = (UB)number;
    return TRUE;
}
/*
 * トークン分割
 */
static int split_args(char *line, char *argv[], int max_args){
    int argc = 0;
    char *p = line;
    while(*p != '\0'){
        while(*p == ' ' || *p == '\t')  p++;
        if(*p == '\0')  break;
        if(argc >= max_args) break;
        argv[argc++] = p;
        while(*p != '\0' && *p != ' ' && *p != '\t')  p++;
        if(*p == '\0')  break;
        *p = '\0';
        p++;
    }
    return argc;
}
/*
 * コマンド:Help (Help表示)
 */
static void cmd_help(int argc, char *argv[]){
    (void)argc;
    (void)argv;
    uart_tx_send("commands:\r\n");
    for(int i=0; i<command_count; i++){
        uart_tx_printf("   %s -  %s\r\n", command_table[i].name, command_table[i].help);
    }
}
/*
 * コマンド:status　(ステータス表示)
 */
static void cmd_status(int argc, char *argv[]){
   UW overflow_count;
   (void)argc; (void)argv;
   overflow_count = uart_tx_get_overflow_count();
    uart_tx_send("TryKernel status: running\r\n");
    uart_tx_printf("LED mode: %s\r\n", led_task_mode_name());
    uart_tx_printf("UART TX queue: %s\r\n",
        overflow_count != 0U ? "overflow detected" : "OK") ;
    uart_tx_printf("UART TX overflow count: %u\r\n", (UINT)overflow_count);
    uart_tx_printf("UART RX overflow count: %u\r\n", (UINT)uart_rx_overflow_count());
    uart_tx_printf("UART RX HW overrun count: %u\r\n", (UINT)uart_rx_hw_overrun_count());
    uart_tx_printf("UART RX IRQ count: %u\r\n", (UINT)uart_rx_irq_count_get());
    uart_tx_printf("UART RX timeout IRQ count: %u\r\n", (UINT)uart_rt_irq_count_get());
}
/*
 * コマンド:Echo (Echo 表示)
 */
static void cmd_echo(int argc, char *argv[]){
    char text[CMD_OUTPUT_BUF_SIZE];
    UW pos = 0U;
    /*
     * 引数を空白で連結 最後のCR/LF/NULLに3バイト残す
     */
    for(int i=1; i<argc; i++){
        const char *src = argv[i];
        while((*src != '\0') && (pos < (CMD_OUTPUT_BUF_SIZE - 3U))){
            text[pos++] = *src++;
        }
        if((i != (argc - 1)) && (pos < (CMD_OUTPUT_BUF_SIZE - 3U))){
            text[pos] = ' ';
            pos++;
        }
    }
    text[pos++] = '\r';
    text[pos++] = '\n';
    text[pos] = '\0';

    uart_tx_send(text);
}
/*
 * コマンド:LED (LED表示)
 */
static void cmd_led(int argc, char *argv[]){
    if (argc < 2){
        uart_tx_send("usage: led on|off|blink\r\n");
        return ;
    }else if (str_eq(argv[1], "on") == TRUE){
        led_task_set_on();
        uart_tx_send("led on\r\n");
    }else if (str_eq(argv[1], "off") == TRUE){
        led_task_set_off();
        uart_tx_send("led off\r\n");
    }else if (str_eq(argv[1], "blink") == TRUE){
        led_task_blink();
        uart_tx_send("led blink\r\n");
    }else{
        uart_tx_send("usage: led on|off|blink\r\n");
    }
}
/*
 * mini_printf()テスト
 */
static void cmd_print(int argc, char* argv[]){
    (void)argc;
    (void)argv;
    uart_tx_printf(
        "test: %s %c %d %u %x %%\r\n",
        "abc", 'Z', -123, 456U, 0x1a2b3cU
    );
}
static void cmd_i2cscan(int argc, char* argv[]){
    UW found = 0U;
    (void)argc;
    (void)argv;
    uart_tx_send("Scanning I2C bus...\r\n");
    for(UB addr = 0x08; addr <= 0x77; addr++){
        if(i2c0_probe(addr)){
            uart_tx_printf("Found device at 0x%x\r\n", addr);
            found++;
        }
    }
    uart_tx_printf("I2C scan complete. Found %u device(s).\r\n", (UINT)found);
}
static void cmd_temperature(int argc, char *argv[])
{
    INT temperature_milli_c;
    UINT magnitude;
    UINT integer_part;
    UINT fractional_part;
    const char *sign;

    (void)argc;
    (void)argv;

    if(adt7410_read_temperature(
            &temperature_milli_c) == FALSE){
        uart_tx_send("ADT7410 read error\r\n");
        return;
    }

    if(temperature_milli_c < 0){
        sign = "-";
        magnitude = (UINT)(-temperature_milli_c);
    }else{
        sign = "";
        magnitude = (UINT)temperature_milli_c;
    }

    integer_part = magnitude / 1000U;
    fractional_part = magnitude % 1000U;

    if(fractional_part < 10U){
        uart_tx_printf(
            "ADT7410 temperature: %s%u.00%u C\r\n",
            sign,
            integer_part,
            fractional_part
        );
    }else if(fractional_part < 100U){
        uart_tx_printf(
            "ADT7410 temperature: %s%u.0%u C\r\n",
            sign,
            integer_part,
            fractional_part
        );
    }else{
        uart_tx_printf(
            "ADT7410 temperature: %s%u.%u C\r\n",
            sign,
            integer_part,
            fractional_part
        );
    }
}

static void cmd_adtinfo(int argc, char *argv[])
{
    UB device_id;
    UB configuration;
    UB operation_mode;
    const char *operation_mode_name;

    (void)argc;
    (void)argv;

    if(adt7410_read_device_info(
            &device_id,
            &configuration) == FALSE){
        uart_tx_send("ADT7410 diagnostic read error\r\n");
        return;
    }

    operation_mode = (configuration >> 5) & 0x03U;
    if(operation_mode == 0U){
        operation_mode_name = "continuous";
    }else if(operation_mode == 1U){
        operation_mode_name = "one shot";
    }else if(operation_mode == 2U){
        operation_mode_name = "1 SPS";
    }else{
        operation_mode_name = "shutdown";
    }

    uart_tx_printf("ADT7410 ID: 0x%x\r\n", (UINT)device_id);
    uart_tx_printf(
        "  manufacturer ID: 0x%x\r\n",
        (UINT)(device_id >> 3)
    );
    uart_tx_printf(
        "  revision ID: 0x%x\r\n",
        (UINT)(device_id & 0x07U)
    );
    uart_tx_printf(
        "ADT7410 configuration: 0x%x\r\n",
        (UINT)configuration
    );
    uart_tx_printf(
        "  resolution: %s\r\n",
        ((configuration & 0x80U) != 0U) ? "16 bit" : "13 bit"
    );
    uart_tx_printf(
        "  operation mode: %s\r\n",
        operation_mode_name
    );
}

static void cmd_adtconfig(int argc, char *argv[])
{
    BOOL resolution_16bit;

    if(argc != 2){
        uart_tx_send("usage: adtconfig 13|16\r\n");
        return;
    }

    if(str_eq(argv[1], "13") == TRUE){
        resolution_16bit = FALSE;
    }else if(str_eq(argv[1], "16") == TRUE){
        resolution_16bit = TRUE;
    }else{
        uart_tx_send("usage: adtconfig 13|16\r\n");
        return;
    }

    if(adt7410_set_resolution(resolution_16bit) == FALSE){
        uart_tx_send("ADT7410 configuration error\r\n");
        return;
    }

    uart_tx_printf(
        "ADT7410 resolution: %s\r\n",
        (resolution_16bit != FALSE) ? "16 bit" : "13 bit"
    );
}

static void cmd_adtraw(int argc, char *argv[])
{
    UINT raw_temperature;
    BOOL resolution_16bit;

    (void)argc;
    (void)argv;

    if(adt7410_read_raw_temperature(
            &raw_temperature,
            &resolution_16bit) == FALSE){
        uart_tx_send("ADT7410 raw read error\r\n");
        return;
    }

    uart_tx_printf(
        "ADT7410 raw temperature: 0x%x\r\n",
        raw_temperature
    );
    uart_tx_printf(
        "  resolution: %s\r\n",
        (resolution_16bit != FALSE) ? "16 bit" : "13 bit"
    );

    if(resolution_16bit == FALSE){
        uart_tx_printf(
            "  flags: TLOW=%u THIGH=%u TCRIT=%u\r\n",
            raw_temperature & 0x01U,
            (raw_temperature >> 1) & 0x01U,
            (raw_temperature >> 2) & 0x01U
        );
    }
}

static void cmd_inaid(int argc, char *argv[])
{
    UINT manufacturer_id;
    UINT die_id;

    (void)argc;
    (void)argv;

    if(ina226_read_ids(&manufacturer_id, &die_id) == FALSE){
        uart_tx_send("INA226 identification read error\r\n");
        return;
    }

    uart_tx_printf("INA226 Manufacturer ID: 0x%x\r\n", manufacturer_id);
    uart_tx_printf("INA226 Die ID: 0x%x\r\n", die_id);

    if((manufacturer_id == INA226_EXPECTED_MANUFACTURER_ID)
            && (die_id == INA226_EXPECTED_DIE_ID)){
        uart_tx_send("  device: INA226\r\n");
    }else{
        uart_tx_send("  unexpected device ID\r\n");
    }
}

static void cmd_inabus(int argc, char *argv[])
{
    UINT raw_value;
    UW voltage_mv;

    (void)argc;
    (void)argv;

    if(ina226_read_bus_voltage(&raw_value, &voltage_mv) == FALSE){
        uart_tx_send("INA226 bus voltage read error\r\n");
        return;
    }

    uart_tx_printf("INA226 bus voltage raw: 0x%x\r\n", raw_value);
    uart_tx_printf("INA226 bus voltage: %u mV\r\n", (UINT)voltage_mv);
}

static void cmd_inashunt(int argc, char *argv[])
{
    INT raw_value;
    INT shunt_voltage_uv;
    INT current_ua;

    (void)argc;
    (void)argv;

    if(ina226_read_shunt(
            &raw_value, &shunt_voltage_uv, &current_ua) == FALSE){
        uart_tx_send("INA226 shunt voltage read error\r\n");
        return;
    }

    uart_tx_printf("INA226 shunt voltage raw: %d\r\n", raw_value);
    uart_tx_printf("INA226 shunt voltage: %d uV\r\n", shunt_voltage_uv);
    uart_tx_printf("INA226 current: %d uA\r\n", current_ua);
}

static void cmd_inapower(int argc, char *argv[])
{
    INA226_MEASUREMENT measurement;
    UW error_count;

    (void)argc;
    (void)argv;

    if(task_inapower_get(&measurement, &error_count) == FALSE){
        uart_tx_printf(
            "INA226 periodic measurement is not ready (errors=%u)\r\n",
            (UINT)error_count
        );
        return;
    }

    uart_tx_printf(
        "INA226 periodic measurement: sample=%u errors=%u\r\n",
        (UINT)measurement.sample_count,
        (UINT)error_count
    );
    uart_tx_printf(
        "INA226 bus voltage: %u mV\r\n",
        (UINT)measurement.bus_voltage_mv
    );
    uart_tx_printf("INA226 current: %d uA\r\n", measurement.current_ua);
    uart_tx_printf("INA226 power: %d mW\r\n", measurement.power_mw);
}

static void cmd_mpuid(int argc, char *argv[])
{
    UB device_id;

    (void)argc;
    (void)argv;

    if(mpu6050_read_who_am_i(&device_id) == FALSE){
        uart_tx_send("MPU-6050 WHO_AM_I read error\r\n");
        return;
    }

    uart_tx_printf("MPU-6050 WHO_AM_I: 0x%x\r\n", (UINT)device_id);

    if(device_id == MPU6050_WHO_AM_I){
        uart_tx_send("  device: MPU-6050\r\n");
    }else if(device_id == MPU6500_WHO_AM_I){
        uart_tx_send("  device: MPU-6500 compatible\r\n");
    }else{
        uart_tx_send("  unexpected device ID\r\n");
    }
}

static void cmd_mpuraw(int argc, char *argv[])
{
    mpu6050_raw_data_t raw_data;

    (void)argc;
    (void)argv;

    if(mpu6050_init() == FALSE){
        uart_tx_send("MPU-6050 initialization error\r\n");
        return;
    }

    if(mpu6050_read_raw(&raw_data) == FALSE){
        uart_tx_send("MPU-6050 raw data read error\r\n");
        return;
    }

    uart_tx_printf(
        "MPU-6050 accel raw: X=%d Y=%d Z=%d\r\n",
        raw_data.accel_x,
        raw_data.accel_y,
        raw_data.accel_z
    );
    uart_tx_printf(
        "MPU-6050 gyro raw:  X=%d Y=%d Z=%d\r\n",
        raw_data.gyro_x,
        raw_data.gyro_y,
        raw_data.gyro_z
    );
    uart_tx_printf(
        "MPU-6050 temperature raw: %d\r\n",
        raw_data.temperature
    );
}

static void format_milli_value(
    INT value,
    const char *unit,
    char *text,
    UW text_size
)
{
    UINT magnitude;
    UINT integer_part;
    UINT fractional_part;
    const char *sign;

    if(value < 0){
        sign = "-";
        magnitude = (UINT)(-value);
    }else{
        sign = "";
        magnitude = (UINT)value;
    }

    integer_part = magnitude / 1000U;
    fractional_part = magnitude % 1000U;

    /* mini_printfは桁指定に対応していないため手動でゼロを補う */
    if(fractional_part < 10U){
        mini_snprintf(
            text,
            text_size,
            "%s%u.00%u %s",
            sign,
            integer_part,
            fractional_part,
            unit
        );
    }else if(fractional_part < 100U){
        mini_snprintf(
            text,
            text_size,
            "%s%u.0%u %s",
            sign,
            integer_part,
            fractional_part,
            unit
        );
    }else{
        mini_snprintf(
            text,
            text_size,
            "%s%u.%u %s",
            sign,
            integer_part,
            fractional_part,
            unit
        );
    }
}

static void cmd_mpu(int argc, char *argv[])
{
    mpu6050_raw_data_t raw_data;
    UB device_id;
    INT accel_x_milli_g;
    INT accel_y_milli_g;
    INT accel_z_milli_g;
    INT gyro_x_milli_dps;
    INT gyro_y_milli_dps;
    INT gyro_z_milli_dps;
    INT temperature_milli_c;
    char accel_x_text[24];
    char accel_y_text[24];
    char accel_z_text[24];
    char gyro_x_text[24];
    char gyro_y_text[24];
    char gyro_z_text[24];
    char temperature_text[24];

    (void)argc;
    (void)argv;

    if(mpu6050_read_who_am_i(&device_id) == FALSE
            || mpu6050_is_supported_device(device_id) == FALSE){
        uart_tx_send("MPU sensor identification error\r\n");
        return;
    }

    if(mpu6050_init() == FALSE
            || mpu6050_read_raw(&raw_data) == FALSE){
        uart_tx_send("MPU sensor read error\r\n");
        return;
    }

    /* 初期レンジ: 加速度±2g、ジャイロ±250dps */
    accel_x_milli_g = (raw_data.accel_x * 1000) / 16384;
    accel_y_milli_g = (raw_data.accel_y * 1000) / 16384;
    accel_z_milli_g = (raw_data.accel_z * 1000) / 16384;
    if(mpu6050_get_gyro_milli_dps(
            &raw_data,
            &gyro_x_milli_dps,
            &gyro_y_milli_dps,
            &gyro_z_milli_dps) == FALSE){
        uart_tx_send("MPU gyro conversion error\r\n");
        return;
    }

    if(device_id == MPU6500_WHO_AM_I){
        temperature_milli_c =
            (INT)(((D)raw_data.temperature * 100000) / 33387) + 21000;
    }else{
        temperature_milli_c =
            (raw_data.temperature * 50) / 17 + 36530;
    }

    format_milli_value(
        accel_x_milli_g, "g", accel_x_text, sizeof(accel_x_text));
    format_milli_value(
        accel_y_milli_g, "g", accel_y_text, sizeof(accel_y_text));
    format_milli_value(
        accel_z_milli_g, "g", accel_z_text, sizeof(accel_z_text));
    format_milli_value(
        gyro_x_milli_dps, "dps", gyro_x_text, sizeof(gyro_x_text));
    format_milli_value(
        gyro_y_milli_dps, "dps", gyro_y_text, sizeof(gyro_y_text));
    format_milli_value(
        gyro_z_milli_dps, "dps", gyro_z_text, sizeof(gyro_z_text));
    format_milli_value(
        temperature_milli_c,
        "C",
        temperature_text,
        sizeof(temperature_text)
    );

    uart_tx_printf(
        "Acceleration: X=%s Y=%s Z=%s\r\n",
        accel_x_text,
        accel_y_text,
        accel_z_text
    );
    uart_tx_printf(
        "Gyroscope:    X=%s Y=%s Z=%s\r\n",
        gyro_x_text,
        gyro_y_text,
        gyro_z_text
    );
    uart_tx_printf("Temperature:  %s\r\n", temperature_text);
}

static void cmd_mpucal(int argc, char *argv[])
{
    INT offset_x;
    INT offset_y;
    INT offset_z;

    (void)argc;
    (void)argv;

    uart_tx_send("Keep the MPU sensor still...\r\n");
    tk_dly_tsk(1000);

    if(mpu6050_calibrate_gyro(
            200U,
            10,
            &offset_x,
            &offset_y,
            &offset_z) == FALSE){
        uart_tx_send("MPU gyro calibration error\r\n");
        return;
    }

    uart_tx_send("MPU gyro calibration complete\r\n");
    uart_tx_printf(
        "Gyro offset raw: X=%d Y=%d Z=%d\r\n",
        offset_x,
        offset_y,
        offset_z
    );
}

static void cmd_mpuirq(int argc, char *argv[])
{
    const char *operation;
    const char *stage;

    (void)argc;
    (void)argv;

    uart_tx_printf("MPU GPIO IRQ count: %u\r\n", (UINT)gpio_irq_count_get());
    uart_tx_printf(
        "MPU sample count: %u\r\n",
        (UINT)task_mpuirq_sample_count_get()
    );
    uart_tx_printf(
        "MPU IRQ error count: %u\r\n",
        (UINT)task_mpuirq_error_count_get()
    );
    uart_tx_printf(
        "Motion measurement: %s\r\n",
        (task_mpuirq_motion_is_enabled() != FALSE) ? "ON" : "OFF"
    );
    uart_tx_printf(
        "I2C error count: %u\r\n",
        (UINT)i2c0_error_count_get()
    );
    uart_tx_printf(
        "I2C recovery count: %u\r\n",
        (UINT)i2c0_recovery_count_get()
    );

    operation = (i2c0_last_error_operation_get() == 1U)
        ? "write" : "write-read";
    if(i2c0_last_error_stage_get() == 1U){
        stage = "disable";
    }else if(i2c0_last_error_stage_get() == 2U){
        stage = "enable";
    }else if(i2c0_last_error_stage_get() == 3U){
        stage = "tx";
    }else if(i2c0_last_error_stage_get() == 4U){
        stage = "rx";
    }else if(i2c0_last_error_stage_get() == 5U){
        stage = "stop";
    }else{
        stage = "none";
    }

    if(i2c0_error_count_get() != 0U){
        uart_tx_printf(
            "Last I2C error: addr=0x%x op=%s stage=%s\r\n",
            (UINT)i2c0_last_error_address_get(),
            operation,
            stage
        );
        uart_tx_printf(
            "I2C TX_ABRT_SOURCE: 0x%x\r\n",
            (UINT)i2c0_last_abort_source_get()
        );
    }
}

static void cmd_motion(int argc, char *argv[])
{
    INT offset_x;
    INT offset_y;
    INT offset_z;
    long long accel_reference_squared;

    (void)argc;
    (void)argv;

    task_mpuirq_motion_stop();
    task_motionled_set_settling();
    uart_tx_send(
        "Motion settling mode started. Keep the MPU sensor still for 3 seconds...\r\n");

    if(mpu6050_calibrate_motion(
            300U,
            10,
            &offset_x,
            &offset_y,
            &offset_z,
            &accel_reference_squared) == FALSE){
        task_motionled_set_off();
        uart_tx_send("Motion settling error\r\n");
        return;
    }

    uart_tx_send("Motion settling complete\r\n");
    uart_tx_printf(
        "Gyro offset raw: X=%d Y=%d Z=%d\r\n",
        offset_x,
        offset_y,
        offset_z
    );
    task_mpuirq_motion_start(accel_reference_squared);
    uart_tx_send("Motion measurement mode started\r\n");
    uart_tx_send("Motion: STOPPED\r\n");
}

static void cmd_lcdtest(int argc, char *argv[])
{
    (void)argc;
    (void)argv;

    if(grove_lcd_init() == FALSE){
        uart_tx_send("Grove LCD initialization error\r\n");
        return;
    }

    if(grove_lcd_update_lines("Try Kernel", "LCD test") == FALSE){
        uart_tx_send("Grove LCD write error\r\n");
        return;
    }

    uart_tx_send("Grove LCD test complete\r\n");
}

static void format_temperature_line(INT temperature_milli_c, char *line)
{
    UINT magnitude;
    UINT integer_part;
    UINT fractional_part;
    UINT divisor;
    UW pos = 0U;

    for(UW i = 0U; i < GROVE_LCD_COLUMNS; i++){
        line[i] = ' ';
    }
    line[GROVE_LCD_COLUMNS] = '\0';

    if(temperature_milli_c < 0){
        line[pos++] = '-';
        magnitude = (UINT)(-temperature_milli_c);
    }else{
        magnitude = (UINT)temperature_milli_c;
    }

    integer_part = magnitude / 1000U;
    fractional_part = magnitude % 1000U;

    divisor = 1U;
    while((integer_part / divisor) >= 10U){
        divisor *= 10U;
    }

    do{
        line[pos++] = (char)('0' + (integer_part / divisor));
        integer_part %= divisor;
        divisor /= 10U;
    }while(divisor != 0U);

    line[pos++] = '.';
    line[pos++] = (char)('0' + (fractional_part / 100U));
    line[pos++] = (char)('0' + ((fractional_part / 10U) % 10U));
    line[pos++] = (char)('0' + (fractional_part % 10U));
    line[pos++] = ' ';
    line[pos] = 'C';
}

static void cmd_lcdtemp(int argc, char *argv[])
{
    INT temperature_milli_c;
    char temperature_line[GROVE_LCD_COLUMNS + 1U];

    (void)argc;
    (void)argv;

    if(adt7410_read_temperature(&temperature_milli_c) == FALSE){
        uart_tx_send("ADT7410 read error\r\n");
        return;
    }

    format_temperature_line(temperature_milli_c, temperature_line);

    if(grove_lcd_init() == FALSE){
        uart_tx_send("Grove LCD initialization error\r\n");
        return;
    }

    if(grove_lcd_update_lines(
            "ADT7410 Temp    ",
            temperature_line) == FALSE){
        uart_tx_send("Grove LCD write error\r\n");
        return;
    }

    uart_tx_send("ADT7410 temperature displayed on LCD\r\n");
}

static void cmd_lcdcolor(int argc, char *argv[])
{
    UB red;
    UB green;
    UB blue;

    if((argc != 4)
            || (parse_u8(argv[1], &red) == FALSE)
            || (parse_u8(argv[2], &green) == FALSE)
            || (parse_u8(argv[3], &blue) == FALSE)){
        uart_tx_send("usage: lcdcolor R G B (0-255)\r\n");
        return;
    }

    if(grove_lcd_set_rgb(red, green, blue) == FALSE){
        uart_tx_send("Grove LCD backlight error\r\n");
        return;
    }

    uart_tx_printf(
        "Grove LCD color: %u %u %u\r\n",
        (UINT)red,
        (UINT)green,
        (UINT)blue
    );
}

static const char *lcd_mode_name(lcd_display_mode_t mode)
{
    if(mode == LCD_MODE_ACCELERATION){
        return "accel";
    }
    if(mode == LCD_MODE_GYROSCOPE){
        return "gyro";
    }
    return "temp";
}

static void cmd_lcdmode(int argc, char *argv[])
{
    lcd_display_mode_t mode;

    if(argc == 1){
        uart_tx_printf(
            "LCD mode: %s\r\n",
            lcd_mode_name(task_lcdtemp_get_mode())
        );
        return;
    }

    if(argc != 2){
        uart_tx_send("usage: lcdmode temp|accel|gyro\r\n");
        return;
    }

    if(str_eq(argv[1], "temp") == TRUE){
        mode = LCD_MODE_TEMPERATURE;
    }else if(str_eq(argv[1], "accel") == TRUE){
        mode = LCD_MODE_ACCELERATION;
    }else if(str_eq(argv[1], "gyro") == TRUE){
        mode = LCD_MODE_GYROSCOPE;
    }else{
        uart_tx_send("usage: lcdmode temp|accel|gyro\r\n");
        return;
    }

    task_lcdtemp_set_mode(mode);
    uart_tx_printf("LCD mode: %s\r\n", lcd_mode_name(mode));
}

static void cmd_msgsend(int argc, char *argv[])
{
    UW sequence;
    ER err;

    (void)argc;
    (void)argv;

    err = task_msgtest_send(&sequence);
    if(err == E_OK) {
        uart_tx_printf("Message sent: sequence=%u\r\n", (UINT)sequence);
    } else {
        uart_tx_printf("Message send error: %d\r\n", err);
    }
}

static void cmd_msgtest(int argc, char *argv[])
{
    (void)argc;
    (void)argv;

    task_msgtest_run_tests();
}

static void cmd_flashid(int argc, char *argv[])
{
    w25qxx_jedec_id_t jedec_id;
    UW capacity;

    (void)argc;
    (void)argv;

    if(w25qxx_read_jedec_id(&jedec_id) == FALSE){
        uart_tx_send("SPI flash JEDEC ID read error\r\n");
        return;
    }

    uart_tx_printf(
        "SPI flash JEDEC ID: 0x%x 0x%x 0x%x\r\n",
        (UINT)jedec_id.manufacturer_id,
        (UINT)jedec_id.memory_type,
        (UINT)jedec_id.capacity_id
    );
    uart_tx_printf(
        "Manufacturer: %s\r\n",
        w25qxx_manufacturer_name(jedec_id.manufacturer_id)
    );

    capacity = w25qxx_capacity_bytes(jedec_id.capacity_id);
    if(capacity != 0U){
        uart_tx_printf(
            "Capacity: %u bytes (%u Mbit)\r\n",
            (UINT)capacity,
            (UINT)(capacity / (128U * 1024U))
        );
    }else{
        uart_tx_send("Capacity: unknown\r\n");
    }
}

static void cmd_flashstatus(int argc, char *argv[])
{
    UB status;

    (void)argc;
    (void)argv;

    if(w25qxx_read_status1(&status) == FALSE){
        uart_tx_send("SPI flash status register-1 read error\r\n");
        return;
    }

    uart_tx_printf("SPI flash status register-1: 0x%x\r\n", (UINT)status);
    uart_tx_printf(
        "  BUSY=%u WEL=%u\r\n",
        ((status & W25QXX_STATUS1_BUSY) != 0U) ? 1U : 0U,
        ((status & W25QXX_STATUS1_WEL) != 0U) ? 1U : 0U
    );
    uart_tx_printf(
        "  BP2=%u BP1=%u BP0=%u TB=%u SEC=%u SRP0=%u\r\n",
        ((status & W25QXX_STATUS1_BP2) != 0U) ? 1U : 0U,
        ((status & W25QXX_STATUS1_BP1) != 0U) ? 1U : 0U,
        ((status & W25QXX_STATUS1_BP0) != 0U) ? 1U : 0U,
        ((status & W25QXX_STATUS1_TB) != 0U) ? 1U : 0U,
        ((status & W25QXX_STATUS1_SEC) != 0U) ? 1U : 0U,
        ((status & W25QXX_STATUS1_SRP0) != 0U) ? 1U : 0U
    );
}

static void cmd_flashwen(int argc, char *argv[])
{
    UB status;

    (void)argc;
    (void)argv;

    if(w25qxx_write_enable() == FALSE
            || w25qxx_read_status1(&status) == FALSE){
        uart_tx_send("SPI flash Write Enable error\r\n");
        (void)w25qxx_write_disable();
        return;
    }

    uart_tx_printf(
        "Write Enable: WEL=%u BUSY=%u\r\n",
        ((status & W25QXX_STATUS1_WEL) != 0U) ? 1U : 0U,
        ((status & W25QXX_STATUS1_BUSY) != 0U) ? 1U : 0U
    );

    if(w25qxx_write_disable() == FALSE
            || w25qxx_read_status1(&status) == FALSE){
        uart_tx_send("SPI flash Write Disable error\r\n");
        return;
    }

    uart_tx_printf(
        "Write Disable: WEL=%u BUSY=%u\r\n",
        ((status & W25QXX_STATUS1_WEL) != 0U) ? 1U : 0U,
        ((status & W25QXX_STATUS1_BUSY) != 0U) ? 1U : 0U
    );
}

static void cmd_flasherase(int argc, char *argv[])
{
    w25qxx_jedec_id_t jedec_id;
    UB verify_buffer[64];
    UW capacity;
    UW test_address;
    UW offset;
    UINT i;

    if((argc != 2) || (str_eq(argv[1], "confirm") == FALSE)){
        uart_tx_send("usage: flasherase confirm\r\n");
        uart_tx_send("WARNING: erases the last 4KB test sector\r\n");
        return;
    }

    if(w25qxx_read_jedec_id(&jedec_id) == FALSE){
        uart_tx_send("SPI flash JEDEC ID read error\r\n");
        return;
    }
    capacity = w25qxx_capacity_bytes(jedec_id.capacity_id);
    if((capacity < W25QXX_SECTOR_SIZE) || (capacity > 0x01000000U)){
        uart_tx_send("Unsupported SPI flash capacity\r\n");
        return;
    }

    test_address = capacity - W25QXX_SECTOR_SIZE;
    uart_tx_printf(
        "Erasing last 4KB test sector at 0x%x...\r\n",
        (UINT)test_address
    );

    if(w25qxx_sector_erase(test_address) == FALSE){
        uart_tx_send("SPI flash sector erase error\r\n");
        return;
    }

    for(offset = 0U; offset < W25QXX_SECTOR_SIZE;
            offset += sizeof(verify_buffer)){
        if(w25qxx_read(
                test_address + offset,
                verify_buffer,
                sizeof(verify_buffer)) == FALSE){
            uart_tx_send("SPI flash erase verify read error\r\n");
            return;
        }
        for(i = 0U; i < sizeof(verify_buffer); i++){
            if(verify_buffer[i] != 0xFFU){
                uart_tx_printf(
                    "SPI flash erase verify error at 0x%x: 0x%x\r\n",
                    (UINT)(test_address + offset + i),
                    (UINT)verify_buffer[i]
                );
                return;
            }
        }
    }

    uart_tx_printf(
        "Sector erase complete: 0x%x-0x%x all 0xff\r\n",
        (UINT)test_address,
        (UINT)(test_address + W25QXX_SECTOR_SIZE - 1U)
    );
}

static void cmd_flashwrite(int argc, char *argv[])
{
    static const UB test_data[] = "TryKernel W25Q32 test";
    w25qxx_jedec_id_t jedec_id;
    UB read_data[sizeof(test_data)];
    UW capacity;
    UW test_address;
    UINT i;

    if((argc != 2) || (str_eq(argv[1], "confirm") == FALSE)){
        uart_tx_send("usage: flashwrite confirm\r\n");
        uart_tx_send("WARNING: programs data in the last 4KB test sector\r\n");
        return;
    }

    if(w25qxx_read_jedec_id(&jedec_id) == FALSE){
        uart_tx_send("SPI flash JEDEC ID read error\r\n");
        return;
    }
    capacity = w25qxx_capacity_bytes(jedec_id.capacity_id);
    if((capacity < W25QXX_SECTOR_SIZE) || (capacity > 0x01000000U)){
        uart_tx_send("Unsupported SPI flash capacity\r\n");
        return;
    }
    test_address = capacity - W25QXX_SECTOR_SIZE;

    if(w25qxx_read(test_address, read_data, sizeof(read_data)) == FALSE){
        uart_tx_send("SPI flash pre-write read error\r\n");
        return;
    }
    for(i = 0U; i < sizeof(read_data); i++){
        if(read_data[i] != 0xFFU){
            uart_tx_send("Test area is not erased; run flasherase confirm first\r\n");
            return;
        }
    }

    uart_tx_printf(
        "Programming %u bytes at 0x%x...\r\n",
        (UINT)sizeof(test_data),
        (UINT)test_address
    );
    if(w25qxx_page_program(
            test_address, test_data, sizeof(test_data)) == FALSE){
        uart_tx_send("SPI flash page program error\r\n");
        return;
    }

    if(w25qxx_read(test_address, read_data, sizeof(read_data)) == FALSE){
        uart_tx_send("SPI flash program verify read error\r\n");
        return;
    }
    for(i = 0U; i < sizeof(read_data); i++){
        if(read_data[i] != test_data[i]){
            uart_tx_printf(
                "SPI flash verify error at 0x%x: wrote=0x%x read=0x%x\r\n",
                (UINT)(test_address + i),
                (UINT)test_data[i],
                (UINT)read_data[i]
            );
            return;
        }
    }

    uart_tx_printf("Page program verify complete: %s\r\n", read_data);
}

static void cmd_flashread(int argc, char *argv[])
{
    w25qxx_jedec_id_t jedec_id;
    UB data[32];
    char text[33];
    UW capacity;
    UW test_address;
    UINT i;

    (void)argc;
    (void)argv;

    if(w25qxx_read_jedec_id(&jedec_id) == FALSE){
        uart_tx_send("SPI flash JEDEC ID read error\r\n");
        return;
    }
    capacity = w25qxx_capacity_bytes(jedec_id.capacity_id);
    if((capacity < W25QXX_SECTOR_SIZE) || (capacity > 0x01000000U)){
        uart_tx_send("Unsupported SPI flash capacity\r\n");
        return;
    }
    test_address = capacity - W25QXX_SECTOR_SIZE;

    if(w25qxx_read(test_address, data, sizeof(data)) == FALSE){
        uart_tx_send("SPI flash test data read error\r\n");
        return;
    }

    uart_tx_printf(
        "SPI flash data at 0x%x (%u bytes):\r\n",
        (UINT)test_address,
        (UINT)sizeof(data)
    );
    for(i = 0U; i < sizeof(data); i += 8U){
        uart_tx_printf(
            "  0x%x: %x %x %x %x %x %x %x %x\r\n",
            (UINT)(test_address + i),
            (UINT)data[i], (UINT)data[i + 1U],
            (UINT)data[i + 2U], (UINT)data[i + 3U],
            (UINT)data[i + 4U], (UINT)data[i + 5U],
            (UINT)data[i + 6U], (UINT)data[i + 7U]
        );
    }

    for(i = 0U; i < sizeof(data); i++){
        if((data[i] >= 0x20U) && (data[i] <= 0x7EU)){
            text[i] = (char)data[i];
        }else{
            text[i] = '.';
        }
    }
    text[sizeof(data)] = '\0';
    uart_tx_printf("ASCII: %s\r\n", text);
}

static void cmd_flashlog(int argc, char *argv[])
{
    (void)argc;
    (void)argv;

    task_flashlog_dump();
}

static void cmd_flashlogclear(int argc, char *argv[])
{
    if((argc != 2) || (str_eq(argv[1], "confirm") == FALSE)){
        uart_tx_send("usage: flashlogclear confirm\r\n");
        uart_tx_send("WARNING: erases the 4KB motion history sector\r\n");
        return;
    }

    uart_tx_send("Erasing motion flash log sector...\r\n");
    if(task_flashlog_clear() == FALSE){
        uart_tx_send("Motion flash log erase error\r\n");
        return;
    }
    uart_tx_send("Motion flash log erase complete\r\n");
}
