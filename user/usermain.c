#include <trykernel.h>
#include "gpio.h"
#include "uart_sync.h"
#include "uart_tx.h"
#include "task_uartrx.h"
#include "task_lcdtemp.h"
#include "i2c.h"
#include "grove_lcd.h"
#include "task_mpuirq.h"
#include "task_msgtest.h"
#include "task_motionled.h"
#include "spi.h"
#include "w25qxx.h"
#include "task_flashlog.h"
#include "task_inapower.h"

/*
 * タスク優先度:1〜16
 * 数字が小さいほど優先度が高い
 */
#define PRIORITY_LED        12
#define PRIORITY_UARTRX     4
#define PRIORITY_UARTTX     6
#define PRIORITY_LOG_A      8
#define PRIORITY_LOG_B      10
#define PRIORITY_LCDTEMP    11
#define PRIORITY_MPUIRQ      9
#define PRIORITY_MSGTEST     7
#define PRIORITY_MOTIONLED   13
#define PRIORITY_FLASHLOG     3
#define PRIORITY_INAPOWER    10
/*
 * stack size
 */
#define STACK_LED       1024
#define STACK_UARTRX    1024
#define STACK_UARTTX    1024
#define STACK_LOG_A     1024
#define STACK_LOG_B     1024
#define STACK_LCDTEMP   1024
#define STACK_MPUIRQ    1024
#define STACK_MSGTEST   1024
#define STACK_MOTIONLED 1024
#define STACK_FLASHLOG  1024
#define STACK_INAPOWER  1024
/*
 * tk_set_flg() scheduler呼び出し確認用
 */
#define ENABLE_FLGTEST 0

#if ENABLE_FLGTEST
#define PRIORITY_FLGTEST_A   7
#define PRIORITY_FLGTEST_B   9

#define STACK_FLGTEST_A   1024
#define STACK_FLGTEST_B   1024
UW tskstk_flgtest_a[STACK_FLGTEST_A / sizeof(UW)];
UW tskstk_flgtest_b[STACK_FLGTEST_B / sizeof(UW)];

ID tskid_flgtest_a;
ID tskid_flgtest_b;

extern void task_flgtest_a(INT stacd, void *exinf);
extern void task_flgtest_b(INT stacd, void *exinf);

T_CTSK ctsk_flgtest_a = {
    .tskatr  = TA_HLNG | TA_RNG3 | TA_USERBUF,
    .task    = task_flgtest_a,
    .itskpri = PRIORITY_FLGTEST_A,
    .stksz   = STACK_FLGTEST_A,
    .bufptr  = tskstk_flgtest_a,
};

T_CTSK ctsk_flgtest_b = {
    .tskatr  = TA_HLNG | TA_RNG3 | TA_USERBUF,
    .task    = task_flgtest_b,
    .itskpri = PRIORITY_FLGTEST_B,
    .stksz   = STACK_FLGTEST_B,
    .bufptr  = tskstk_flgtest_b,
};
/* テスト用イベントフラグ */
ID flgtest_id;

void task_flgtest_a(INT stacd, void *exinf)
{
    (void)stacd;
    (void)exinf;
    UINT flgptn;

    tk_wai_flg(
        flgtest_id,
        1U,
        TWF_ORW,       // 複数タスクを起床させるためTWF_BITCLRは指定しない
        &flgptn,
        TMO_FEVR
    );

    while(1) {
        tk_dly_tsk(1000);
    }
}
void task_flgtest_b(INT stacd, void *exinf)
{
    (void)stacd;
    (void)exinf;
    UINT flgptn;

    tk_wai_flg(
        flgtest_id,
        1U,
        TWF_ORW,       // 複数タスクを起床させるためTWF_BITCLRは指定しない
        &flgptn,
        TMO_FEVR
    );

    while(1) {
        tk_dly_tsk(1000);
    }
}
static int create_flag(void)
{
    T_CFLG cflg;
    cflg.flgatr = TA_TFIFO;
    cflg.iflgptn = 0U;

    flgtest_id = tk_cre_flg(&cflg);
    if(flgtest_id < E_OK){
        return (int)flgtest_id;
    }
    return 0;
}
#endif // ENABLE_FLGTEST
/* ------------------------------------------------------------ */
 /*
 * LED制御タスク1生成情報
 */
UW  tskstk_led1[STACK_LED/sizeof(UW)];   // LED制御タスク1のスタック
ID  tskid_led1;                     // LED制御タスク1のID番号
extern void task_led1(INT stacd, void *exinf);
T_CTSK  ctsk_led1 = {
    .tskatr     = TA_HLNG | TA_RNG3 | TA_USERBUF,   // タスク属性
    .task       = task_led1,
    .itskpri    = PRIORITY_LED,
    .stksz      = STACK_LED,
    .bufptr     = tskstk_led1,
};

/* Uart受信タスク生成情報 */
UW  tskstk_uartrx[STACK_UARTRX/sizeof(UW)];   // Uart受信タスクのスタック
ID  tskid_uartrx;                     // Uart受信タスクのID番号
extern void task_uartrx(INT stacd, void *exinf);
T_CTSK  ctsk_uartrx = {
    .tskatr     = TA_HLNG | TA_RNG3 | TA_USERBUF,   // タスク属性
    .task       = task_uartrx,
    .itskpri    = PRIORITY_UARTRX,
    .stksz      = STACK_UARTRX,
    .bufptr     = tskstk_uartrx,
};

/* Uart送信タスク生成情報 */
UW  tskstk_uarttx[STACK_UARTTX/sizeof(UW)];   // Uart送信タスクのスタック
ID  tskid_uarttx;                     // Uart送信タスクのID番号
extern void task_uarttx(INT stacd, void *exinf);
T_CTSK  ctsk_uarttx = {
    .tskatr     = TA_HLNG | TA_RNG3 | TA_USERBUF,   // タスク属性
    .task       = task_uarttx,
    .itskpri    = PRIORITY_UARTTX,
    .stksz      = STACK_UARTTX,
    .bufptr     = tskstk_uarttx,
};


/* UartLog生成情報 */
UW  tskstk_uartlog_a[STACK_LOG_A/sizeof(UW)];
ID  tskid_uartlog_a;
extern void task_uartlog_a(INT stacd, void *exinf);
T_CTSK  ctsk_uartlog_a = {
    .tskatr     = TA_HLNG | TA_RNG3 | TA_USERBUF,   // タスク属性
    .task       = task_uartlog_a,
    .itskpri    = PRIORITY_LOG_A,
    .stksz      = STACK_LOG_A,
    .bufptr     = tskstk_uartlog_a,
};

UW  tskstk_uartlog_b[STACK_LOG_B/sizeof(UW)];
ID  tskid_uartlog_b;
extern void task_uartlog_b(INT stacd, void *exinf);
T_CTSK  ctsk_uartlog_b = {
    .tskatr     = TA_HLNG | TA_RNG3 | TA_USERBUF,   // タスク属性
    .task       = task_uartlog_b,
    .itskpri    = PRIORITY_LOG_B,
    .stksz      = STACK_LOG_B,
    .bufptr     = tskstk_uartlog_b,
};

/* LCD温度表示タスク生成情報 */
UW  tskstk_lcdtemp[STACK_LCDTEMP/sizeof(UW)];
ID  tskid_lcdtemp;
T_CTSK  ctsk_lcdtemp = {
    .tskatr     = TA_HLNG | TA_RNG3 | TA_USERBUF,
    .task       = task_lcdtemp,
    .itskpri    = PRIORITY_LCDTEMP,
    .stksz      = STACK_LCDTEMP,
    .bufptr     = tskstk_lcdtemp,
};

UW tskstk_mpuirq[STACK_MPUIRQ/sizeof(UW)];
ID tskid_mpuirq;
T_CTSK ctsk_mpuirq = {
    .tskatr     = TA_HLNG | TA_RNG3 | TA_USERBUF,
    .task       = task_mpuirq,
    .itskpri    = PRIORITY_MPUIRQ,
    .stksz      = STACK_MPUIRQ,
    .bufptr     = tskstk_mpuirq,
};

UW tskstk_msgtest[STACK_MSGTEST/sizeof(UW)];
ID tskid_msgtest;
T_CTSK ctsk_msgtest = {
    .tskatr     = TA_HLNG | TA_RNG3 | TA_USERBUF,
    .task       = task_msgtest,
    .itskpri    = PRIORITY_MSGTEST,
    .stksz      = STACK_MSGTEST,
    .bufptr     = tskstk_msgtest,
};

UW tskstk_motionled[STACK_MOTIONLED/sizeof(UW)];
ID tskid_motionled;
T_CTSK ctsk_motionled = {
    .tskatr     = TA_HLNG | TA_RNG3 | TA_USERBUF,
    .task       = task_motionled,
    .itskpri    = PRIORITY_MOTIONLED,
    .stksz      = STACK_MOTIONLED,
    .bufptr     = tskstk_motionled,
};

UW tskstk_flashlog[STACK_FLASHLOG/sizeof(UW)];
ID tskid_flashlog;
T_CTSK ctsk_flashlog = {
    .tskatr     = TA_HLNG | TA_RNG3 | TA_USERBUF,
    .task       = task_flashlog,
    .itskpri    = PRIORITY_FLASHLOG,
    .stksz      = STACK_FLASHLOG,
    .bufptr     = tskstk_flashlog,
};

UW tskstk_inapower[STACK_INAPOWER/sizeof(UW)];
ID tskid_inapower;
T_CTSK ctsk_inapower = {
    .tskatr     = TA_HLNG | TA_RNG3 | TA_USERBUF,
    .task       = task_inapower,
    .itskpri    = PRIORITY_INAPOWER,
    .stksz      = STACK_INAPOWER,
    .bufptr     = tskstk_inapower,
};


int usermain(void)
{
    ER ercd;
    led25_init();   // PicoボードのLED初期化
    i2c0_init();   // I2C初期化
    spi0_init();   // SPI0初期化
    w25qxx_init(); // W25QXXのCS初期化
    ercd = i2c0_sync_init();
    if(ercd < E_OK){
        return (int)ercd;
    }
    ercd = w25qxx_sync_init();
    if(ercd < E_OK){
        return (int)ercd;
    }
    ercd = grove_lcd_sync_init();
    if(ercd < E_OK){
        return (int)ercd;
    }
#if ENABLE_FLGTEST
    if(create_flag() < E_OK){
        return -1;
    }
#endif // ENABLE_FLGTEST
    /*
     * UART 送信排他制御用セマフォの生成
     */
    ercd = uart_sync_init();
    if(ercd < E_OK){
        return (int)ercd;
    }
    ercd = uart_tx_init();
    if(ercd < E_OK){
        return (int)ercd;
    }
    ercd = task_uartrx_init();
    if(ercd < E_OK){
        return (int)ercd;
    }
    ercd = task_mpuirq_init();
    if(ercd < E_OK){
        return (int)ercd;
    }
    ercd = task_msgtest_init();
    if(ercd < E_OK){
        return (int)ercd;
    }
    ercd = task_motionled_init();
    if(ercd < E_OK){
        return (int)ercd;
    }
    ercd = task_flashlog_init();
    if(ercd < E_OK){
        return (int)ercd;
    }
    task_inapower_init();
    tk_dly_tsk(5);
    uart_tx_send("Hello Try Kernel\r\n");

    /* Uart送信タスク */
    tskid_uarttx = tk_cre_tsk(&ctsk_uarttx);
    if(tskid_uarttx < E_OK){
        return (int)tskid_uarttx;
    }
    tk_sta_tsk(tskid_uarttx, 0);

    tskid_flashlog = tk_cre_tsk(&ctsk_flashlog);
    if(tskid_flashlog < E_OK){
        return (int)tskid_flashlog;
    }
    tk_sta_tsk(tskid_flashlog, 0);

    tskid_inapower = tk_cre_tsk(&ctsk_inapower);
    if(tskid_inapower < E_OK){
        return (int)tskid_inapower;
    }
    tk_sta_tsk(tskid_inapower, 0);

    /* LED制御タスク1の生成、実行 */
    tskid_led1 = tk_cre_tsk(&ctsk_led1);
    if(tskid_led1 < E_OK){
        return (int)tskid_led1;
    }
    tk_sta_tsk(tskid_led1, 0);

    /* Uart受信タスク */
    tskid_uartrx = tk_cre_tsk(&ctsk_uartrx);
    if(tskid_uartrx < E_OK){
        return (int)tskid_uartrx;
    }
    tk_sta_tsk(tskid_uartrx, 0);

    /* UART log task A */
    tskid_uartlog_a = tk_cre_tsk(&ctsk_uartlog_a);
    if(tskid_uartlog_a < E_OK){
        return (int)tskid_uartlog_a;
    }
    tk_sta_tsk(tskid_uartlog_a, 0);

    /* UART log task B */
    tskid_uartlog_b = tk_cre_tsk(&ctsk_uartlog_b);
    if(tskid_uartlog_b < E_OK){
        return (int)tskid_uartlog_b;
    }
    tk_sta_tsk(tskid_uartlog_b, 0);

    /* LCD温度表示タスク */
    tskid_lcdtemp = tk_cre_tsk(&ctsk_lcdtemp);
    if(tskid_lcdtemp < E_OK){
        return (int)tskid_lcdtemp;
    }
    tk_sta_tsk(tskid_lcdtemp, 0);

    tskid_mpuirq = tk_cre_tsk(&ctsk_mpuirq);
    if(tskid_mpuirq < E_OK){
        return (int)tskid_mpuirq;
    }
    tk_sta_tsk(tskid_mpuirq, 0);

    tskid_msgtest = tk_cre_tsk(&ctsk_msgtest);
    if(tskid_msgtest < E_OK){
        return (int)tskid_msgtest;
    }
    tk_sta_tsk(tskid_msgtest, 0);

    tskid_motionled = tk_cre_tsk(&ctsk_motionled);
    if(tskid_motionled < E_OK){
        return (int)tskid_motionled;
    }
    tk_sta_tsk(tskid_motionled, 0);

#if ENABLE_FLGTEST
    tskid_flgtest_a = tk_cre_tsk(&ctsk_flgtest_a);
    if(tskid_flgtest_a < E_OK){
        return (int)tskid_flgtest_a;
    }
    tk_sta_tsk(tskid_flgtest_a, 0);

    tskid_flgtest_b = tk_cre_tsk(&ctsk_flgtest_b);
    if(tskid_flgtest_b < E_OK){
        return (int)tskid_flgtest_b;
    }
    tk_sta_tsk(tskid_flgtest_b, 0);

    tk_dly_tsk(200);
    tk_set_flg(flgtest_id, 1U); // イベントフラグをセットする
#endif // ENABLE_FLGTEST
    return 0;
}
