/**
 * @file    command.c
 * @brief   UARTコンソールのコマンド解析と実行を行う。
 *
 * @details
 * 入力されたコマンド行を空白とタブで分割し、コマンド名に対応する
 * 処理を実行する。ヘルプや状態表示に加え、LED、モーター、I2C、
 * 距離センサー、ジャイロセンサーを操作・確認するコマンドを提供する。
 * コマンドの結果やエラーはUART経由で出力する。
 *
 */

#include <trykernel.h>
#include "task_led.h"
#include "command.h"
#include "uart_tx.h"
#include "uart.h"
#include "motor.h"
#include "i2c.h"
#include "task_distance.h"
#include "vl53l1x.h"
#include "mpu6050.h"

#define CMD_MAX_ARGS       16
#define CMD_OUTPUT_BUF_SIZE 128U

typedef void (*cmd_func_t)(int argc, char *argv[]);
typedef struct {
    const char *name;
    cmd_func_t func;
    const char *help;
} command_t;

static void cmd_help(int argc, char *argv[]);
static void cmd_status(int argc, char *argv[]);
static void cmd_echo(int argc, char *argv[]);
static void cmd_led(int argc, char *argv[]);
static void cmd_print(int argc, char *argv[]);
static void cmd_motor(int argc, char *argv[]);
static void cmd_drive(int argc, char *argv[]);
static void cmd_i2cscan(int argc, char *argv[]);
static void cmd_distance(int argc, char *argv[]);
static void cmd_gyro(int argc, char *argv[]);
static int split_args(char *line, char *argv[], int max_args);
static BOOL parse_u8(const char *text, UB *value);
static BOOL parse_duration(const char *text, RELTIM *value);
static BOOL str_eq(const char *a, const char *b);

static const command_t command_table[] = {
    {"help",   cmd_help,   "show command list"},
    {"h",      cmd_help,   "show command list"},
    {"status", cmd_status, "show system status"},
    {"echo",   cmd_echo,   "echo arguments"},
    {"led",    cmd_led,    "led on/off/blink"},
    {"print",  cmd_print,  "print test"},
    {"motor",  cmd_motor,  "left|right forward|reverse <0-100>, or stop"},
    {"drive",  cmd_drive,  "forward|reverse|left|right <0-100> [ms], or stop"},
    {"i2cscan", cmd_i2cscan, "scan I2C bus"},
    {"distance", cmd_distance, "measure distance in millimeters"},
    {"gyro", cmd_gyro, "show angular velocity [1-50 samples], or calibrate"}
};

static const int command_count = sizeof(command_table) / sizeof(command_table[0]);

/**
 * @brief 入力されたコマンド行を解析して対応する処理を実行する。
 * @param[in,out] line コマンド行。解析時に空白とタブを終端文字へ置き換える。
 * @details 空行は無視し、登録されていないコマンド名の場合はUARTへ通知する。
 */
void command_execute(char *line)
{
    char *argv[CMD_MAX_ARGS];
    int argc = split_args(line, argv, CMD_MAX_ARGS);
    int i;

    if(argc == 0) return;
    for(i = 0; i < command_count; i++){
        if(str_eq(argv[0], command_table[i].name) == TRUE){
            command_table[i].func(argc, argv);
            return;
        }
    }
    uart_tx_printf("unknown command: %s\r\n", argv[0]);
}

/**
 * @brief 2つの文字列が完全に一致するか判定する。
 * @param[in] a 比較する文字列。
 * @param[in] b 比較する文字列。
 * @retval TRUE 文字列が一致する。
 * @retval FALSE 文字列が一致しない。
 */
static BOOL str_eq(const char *a, const char *b)
{
    while((*a != '\0') && (*b != '\0')){
        if(*a != *b) return FALSE;
        a++;
        b++;
    }
    return (*a == '\0') && (*b == '\0');
}

/**
 * @brief 10進数文字列を8ビット符号なし整数として変換する。
 * @param[in] text 変換する数字のみの文字列。
 * @param[out] value 変換した値を格納する領域。
 * @retval TRUE 変換に成功した。
 * @retval FALSE 引数がNULL、空文字、数字以外、または255を超える値。
 */
static BOOL parse_u8(const char *text, UB *value)
{
    UINT number = 0U;

    if((text == NULL) || (value == NULL) || (*text == '\0')) return FALSE;
    while(*text != '\0'){
        UINT digit;
        if((*text < '0') || (*text > '9')) return FALSE;
        digit = (UINT)(*text - '0');
        if(number > 25U || ((number == 25U) && (digit > 5U))) return FALSE;
        number = (number * 10U) + digit;
        text++;
    }
    *value = (UB)number;
    return TRUE;
}

/**
 * @brief 10進数文字列をミリ秒単位の相対時間として変換する。
 * @param[in] text 変換する数字のみの文字列。
 * @param[out] value 変換した時間を格納する領域。
 * @retval TRUE 0～60000の範囲で変換に成功した。
 * @retval FALSE 引数がNULL、空文字、数字以外、または60000を超える値。
 */
static BOOL parse_duration(const char *text, RELTIM *value)
{
    UW number = 0U;

    if((text == NULL) || (value == NULL) || (*text == '\0')) return FALSE;
    while(*text != '\0'){
        UW digit;
        if((*text < '0') || (*text > '9')) return FALSE;
        digit = (UW)(*text - '0');
        if(number > 6000U || (number == 6000U && digit > 0U)) return FALSE;
        number = (number * 10U) + digit;
        text++;
    }
    *value = (RELTIM)number;
    return TRUE;
}

/**
 * @brief コマンド行を空白またはタブで分割する。
 * @param[in,out] line 分割する文字列。区切り文字を終端文字へ置き換える。
 * @param[out] argv 分割した各引数の先頭を格納する配列。
 * @param[in] max_args argvへ格納できる引数の最大数。
 * @return argvへ格納した引数の数。
 * @note max_argsを超える引数は格納せず、残りの入力を無視する。
 */
static int split_args(char *line, char *argv[], int max_args)
{
    int argc = 0;
    char *p = line;

    while(*p != '\0'){
        while(*p == ' ' || *p == '\t') p++;
        if(*p == '\0' || argc >= max_args) break;
        argv[argc++] = p;
        while(*p != '\0' && *p != ' ' && *p != '\t') p++;
        if(*p == '\0') break;
        *p++ = '\0';
    }
    return argc;
}

/**
 * @brief 登録されているコマンド名と説明をUARTへ表示する。
 * @param[in] argc コマンド引数の数（この関数では使用しない）。
 * @param[in] argv コマンド引数（この関数では使用しない）。
 */
static void cmd_help(int argc, char *argv[])
{
    int i;
    (void)argc;
    (void)argv;
    uart_tx_send("commands:\r\n");
    for(i = 0; i < command_count; i++){
        uart_tx_printf("   %s -  %s\r\n", command_table[i].name, command_table[i].help);
    }
}

/**
 * @brief システム、LED、UARTの状態と各種カウンタをUARTへ表示する。
 * @param[in] argc コマンド引数の数（この関数では使用しない）。
 * @param[in] argv コマンド引数（この関数では使用しない）。
 */
static void cmd_status(int argc, char *argv[])
{
    UW overflow_count;
    (void)argc;
    (void)argv;
    overflow_count = uart_tx_get_overflow_count();
    uart_tx_send("TryKernel 2WD status: running\r\n");
    uart_tx_printf("LED mode: %s\r\n", led_task_mode_name());
    uart_tx_printf("UART TX queue: %s\r\n", overflow_count != 0U ? "overflow detected" : "OK");
    uart_tx_printf("UART TX overflow count: %u\r\n", (UINT)overflow_count);
    uart_tx_printf("UART RX overflow count: %u\r\n", (UINT)uart_rx_overflow_count());
    uart_tx_printf("UART RX HW overrun count: %u\r\n", (UINT)uart_rx_hw_overrun_count());
    uart_tx_printf("UART RX IRQ count: %u\r\n", (UINT)uart_rx_irq_count_get());
    uart_tx_printf("UART RX timeout IRQ count: %u\r\n", (UINT)uart_rt_irq_count_get());
}

/**
 * @brief コマンド引数を空白区切りでUARTへ送り返す。
 * @param[in] argc コマンド名を含む引数の数。
 * @param[in] argv コマンド名と送り返す文字列。
 * @note 出力は固定長バッファの範囲内に切り詰められる。
 */
static void cmd_echo(int argc, char *argv[])
{
    char text[CMD_OUTPUT_BUF_SIZE];
    UINT pos = 0U;
    int i;

    for(i = 1; i < argc; i++){
        const char *src = argv[i];
        while((*src != '\0') && (pos < CMD_OUTPUT_BUF_SIZE - 3U)) text[pos++] = *src++;
        if(i != argc - 1 && pos < CMD_OUTPUT_BUF_SIZE - 3U) text[pos++] = ' ';
    }
    text[pos++] = '\r';
    text[pos++] = '\n';
    text[pos] = '\0';
    uart_tx_send(text);
}

/**
 * @brief LEDを点灯、消灯、または点滅させる。
 * @param[in] argc コマンド名を含む引数の数。
 * @param[in] argv コマンド名と動作指定（on、off、blink）。
 * @details 引数が不足または不正な場合は使用方法をUARTへ表示する。
 */
static void cmd_led(int argc, char *argv[])
{
    if(argc < 2){
        uart_tx_send("usage: led on|off|blink\r\n");
    }else if(str_eq(argv[1], "on") == TRUE){
        led_task_set_on();
        uart_tx_send("led on\r\n");
    }else if(str_eq(argv[1], "off") == TRUE){
        led_task_set_off();
        uart_tx_send("led off\r\n");
    }else if(str_eq(argv[1], "blink") == TRUE){
        led_task_blink();
        uart_tx_send("led blink\r\n");
    }else{
        uart_tx_send("usage: led on|off|blink\r\n");
    }
}

/**
 * @brief UARTへ固定のテスト文字列を出力する。
 * @param[in] argc コマンド引数の数（この関数では使用しない）。
 * @param[in] argv コマンド引数（この関数では使用しない）。
 */
static void cmd_print(int argc, char *argv[])
{
    (void)argc;
    (void)argv;
    uart_tx_send("print test\r\n");
}

/**
 * @brief 左右いずれかのモーターを個別に操作または停止する。
 * @param[in] argc コマンド名を含む引数の数。
 * @param[in] argv モーター側、回転方向、デューティ比、または停止指定。
 * @details 前進指令は障害物状態を確認し、危険または距離不明の場合に拒否する。
 *          操作結果または使用方法をUARTへ表示する。
 */
static void cmd_motor(int argc, char *argv[])
{
    BOOL is_left;
    BOOL forward;
    UB duty;

    if(argc == 2 && str_eq(argv[1], "stop") == TRUE){
        motor_stop_all();
        uart_tx_send("motor: all stopped\r\n");
        return;
    }
    if(argc == 3 && str_eq(argv[2], "stop") == TRUE){
        if(str_eq(argv[1], "left") == TRUE) motor_left_stop();
        else if(str_eq(argv[1], "right") == TRUE) motor_right_stop();
        else goto usage;
        uart_tx_printf("motor: %s stopped\r\n", argv[1]);
        return;
    }
    if(argc != 4) goto usage;
    if(str_eq(argv[1], "left") == TRUE) is_left = TRUE;
    else if(str_eq(argv[1], "right") == TRUE) is_left = FALSE;
    else goto usage;
    if(str_eq(argv[2], "forward") == TRUE) forward = TRUE;
    else if(str_eq(argv[2], "reverse") == TRUE) forward = FALSE;
    else goto usage;
    if(parse_u8(argv[3], &duty) == FALSE || duty > 100U){
        uart_tx_send("motor: duty must be 0 through 100\r\n");
        return;
    }
    if(forward == TRUE && duty != 0U && motor_obstacle_blocked() == TRUE){
        uart_tx_send("motor: obstacle too close or distance unavailable\r\n");
        return;
    }
    if(is_left == TRUE) motor_left_set(forward, duty);
    else motor_right_set(forward, duty);
    uart_tx_printf("motor: %s %s %u%%\r\n", argv[1], argv[2], (UINT)duty);
    return;

usage:
    uart_tx_send("usage: motor left|right forward|reverse <0-100>, or stop\r\n");
}

/**
 * @brief 車体の前後進、左右旋回、または停止を指示する。
 * @param[in] argc コマンド名を含む引数の数。
 * @param[in] argv 進行方向、デューティ比、任意の動作時間、または停止指定。
 * @details 任意の動作時間を指定した場合、時間経過後に減速停止を指示する。
 *          後退以外の走行では障害物状態を確認する。
 */
static void cmd_drive(int argc, char *argv[])
{
    UB duty;
    RELTIM duration;

    if(argc == 2 && str_eq(argv[1], "stop") == TRUE){
        motor_drive_stop();
        uart_tx_send("drive: decelerating\r\n");
        return;
    }
    if((argc != 3 && argc != 4) || parse_u8(argv[2], &duty) == FALSE || duty > 100U){
        uart_tx_send("usage: drive forward|reverse|left|right <0-100> [ms], or stop\r\n");
        return;
    }
    if(argc == 4 && parse_duration(argv[3], &duration) == FALSE){
        uart_tx_send("drive: duration must be 0 through 60000 ms\r\n");
        return;
    }
    if(duty != 0U && motor_obstacle_blocked() == TRUE
            && str_eq(argv[1], "reverse") == FALSE){
        uart_tx_send("drive: obstacle too close or distance unavailable\r\n");
        return;
    }
    if(str_eq(argv[1], "forward") == TRUE) motor_drive_forward(duty);
    else if(str_eq(argv[1], "reverse") == TRUE) motor_drive_reverse(duty);
    else if(str_eq(argv[1], "left") == TRUE) motor_drive_left(duty);
    else if(str_eq(argv[1], "right") == TRUE) motor_drive_right(duty);
    else {
        uart_tx_send("usage: drive forward|reverse|left|right <0-100> [ms], or stop\r\n");
        return;
    }
    uart_tx_printf("drive: %s %u%%\r\n", argv[1], (UINT)duty);
    if(argc == 4){
        (void)tk_dly_tsk(duration);
        motor_drive_stop();
        uart_tx_send("drive: timed run decelerating\r\n");
    }
}

/**
 * @brief I2Cバス上の有効な7ビットアドレス範囲を走査して応答デバイスを表示する。
 * @param[in] argc コマンド引数の数（この関数では使用しない）。
 * @param[in] argv コマンド引数（この関数では使用しない）。
 */
static void cmd_i2cscan(int argc, char *argv[])
{
    UB address;
    UINT found = 0U;
    (void)argc;
    (void)argv;

    uart_tx_send("I2C scan:\r\n");
    for(address = 0x08U; address <= 0x77U; address++){
        if(i2c0_probe(address) == TRUE){
            uart_tx_printf("  0x%x\r\n", (UINT)address);
            found++;
        }
    }
    if(found == 0U) uart_tx_send("  no devices found\r\n");
}

/**
 * @brief 距離センサーの測定値または取得失敗の情報をUARTへ表示する。
 * @param[in] argc コマンド引数の数（この関数では使用しない）。
 * @param[in] argv コマンド引数（この関数では使用しない）。
 * @details 有効な平均値、最新値、サンプル数を表示する。
 *          有効な測定値がない場合は利用可能な生データまたは失敗理由を表示する。
 */
static void cmd_distance(int argc, char *argv[])
{
    UH average_mm;
    UH latest_mm;
    UH raw_mm;
    UB raw_status;
    UINT sample_count;
    (void)argc;
    (void)argv;

    if(task_distance_get(&average_mm, &latest_mm, &sample_count) == FALSE){
        if(vl53l1x_get_last_result(&raw_mm, &raw_status) == TRUE){
            uart_tx_printf("distance: invalid range, raw %u mm, raw_status %u\r\n",
                           (UINT)raw_mm, (UINT)raw_status);
        }else{
            uart_tx_send("distance: no data (communication/initialization/timeout)\r\n");
        }
        return;
    }
    uart_tx_printf("distance: average %u mm, latest %u mm, samples %u (valid)\r\n",
                   (UINT)average_mm, (UINT)latest_mm, sample_count);
}

/**
 * @brief MPU6050のジャイロ値を表示するか、ジャイロオフセットを校正する。
 * @param[in] argc コマンド名を含む引数の数。
 * @param[in] argv サンプル数（1～50）、または校正指定（calibrate）。
 * @details 必要に応じてデバイス確認と初期化を行う。校正時はモーターを停止し、
 *          静止状態で測定したオフセットを保存する。読み出した角速度はmdps単位で表示する。
 */
static void cmd_gyro(int argc, char *argv[])
{
    static BOOL initialized;
    static BOOL calibrated;
    BOOL calibrate = (argc == 2 && str_eq(argv[1], "calibrate") == TRUE);
    UB device_id;
    UB count = 1U;
    mpu6050_raw_data_t raw;
    INT x, y, z;

    if(argc > 2 || (argc == 2 && calibrate == FALSE &&
            (parse_u8(argv[1], &count) == FALSE || count == 0U || count > 50U))){
        uart_tx_send("usage: gyro [1-50 samples], or gyro calibrate\r\n");
        return;
    }
    if(mpu6050_read_who_am_i(&device_id) == FALSE){
        initialized = FALSE;
        uart_tx_send("gyro: WHO_AM_I read failed\r\n");
        return;
    }
    uart_tx_printf("gyro: WHO_AM_I 0x%x\r\n", (UINT)device_id);
    if(mpu6050_is_supported_device(device_id) == FALSE){
        initialized = FALSE;
        uart_tx_send("gyro: unsupported device\r\n");
        return;
    }
    if(initialized == FALSE){
        if(mpu6050_init() == FALSE){
            uart_tx_send("gyro: initialization failed\r\n");
            return;
        }
        (void)tk_dly_tsk(100);
        initialized = TRUE;
    }
    if(calibrate == TRUE){
        motor_stop_all();
        uart_tx_send("gyro: motors stopped; keep board still for 3 seconds\r\n");
        (void)tk_dly_tsk(1000);
        if(mpu6050_calibrate_gyro(200U, 10, &x, &y, &z) == FALSE){
            initialized = FALSE;
            uart_tx_send("gyro: calibration failed; previous offsets retained\r\n");
            return;
        }
        calibrated = TRUE;
        uart_tx_printf("gyro: calibration complete; raw offsets x %d, y %d, z %d\r\n", x, y, z);
        return;
    }
    uart_tx_printf("gyro: angular velocity, 1000 mdps = 1 deg/s (%s)\r\n",
                   calibrated == TRUE ? "calibrated" : "uncalibrated");
    for(UINT i = 0U; i < count; i++){
        if(mpu6050_read_raw(&raw) == FALSE){
            initialized = FALSE;
            uart_tx_send("gyro: read failed\r\n");
            return;
        }
        if(mpu6050_get_gyro_milli_dps(&raw, &x, &y, &z) == FALSE) return;
        uart_tx_printf("gyro: x %d, y %d, z %d mdps\r\n", x, y, z);
        if(i + 1U < count) (void)tk_dly_tsk(100);
    }
}
