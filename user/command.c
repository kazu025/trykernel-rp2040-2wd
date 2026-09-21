#include <trykernel.h>
#include "task_led.h"
#include "command.h"
#include "uart_tx.h"
#include "uart.h"
#include "motor.h"

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
    {"drive",  cmd_drive,  "forward|reverse|left|right <0-100>, or stop"}
};

static const int command_count = sizeof(command_table) / sizeof(command_table[0]);

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

static BOOL str_eq(const char *a, const char *b)
{
    while((*a != '\0') && (*b != '\0')){
        if(*a != *b) return FALSE;
        a++;
        b++;
    }
    return (*a == '\0') && (*b == '\0');
}

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

static void cmd_print(int argc, char *argv[])
{
    (void)argc;
    (void)argv;
    uart_tx_send("print test\r\n");
}

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
    if(is_left == TRUE) motor_left_set(forward, duty);
    else motor_right_set(forward, duty);
    uart_tx_printf("motor: %s %s %u%%\r\n", argv[1], argv[2], (UINT)duty);
    return;

usage:
    uart_tx_send("usage: motor left|right forward|reverse <0-100>, or stop\r\n");
}

static void cmd_drive(int argc, char *argv[])
{
    UB duty;
    RELTIM duration;

    if(argc == 2 && str_eq(argv[1], "stop") == TRUE){
        motor_stop_all();
        uart_tx_send("drive: stopped\r\n");
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
        motor_stop_all();
        uart_tx_send("drive: timed run stopped\r\n");
    }
}
