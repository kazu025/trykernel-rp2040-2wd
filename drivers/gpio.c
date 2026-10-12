/**
 * @file gpio.c
 * @brief RP2040のGPIOを制御するドライバ。
 *
 * レジスタを直接操作し、GPIOの出力設定と値の変更、
 * 立ち上がりエッジ割り込み、Pico搭載LEDの制御、
 * I2CおよびSPI用のピン設定を行う。
 */
#include "typedef.h"
#include "sysdef.h"
#include "gpio.h"
#include "syslib.h"

extern void out_w(UW addr, UW data);
extern UW in_w(UW addr);

/** @brief 割り込み対象として設定したGPIO番号。現在の実装では設定後に参照されない。 */
static unsigned int gpio_irq_pin;

/** @brief 対象GPIOが属するIO_BANK0割り込みレジスタのグループ番号。 */
static UW gpio_irq_group;

/** @brief 対象GPIOの立ち上がりエッジ割り込みを示すビットマスク。 */
static UW gpio_irq_mask;

/** @brief 割り込み検出時に呼び出す通知関数。 */
static GPIO_IRQ_NOTIFY_FUNC gpio_irq_notify;

/** @brief 検出した立ち上がりエッジ割り込みの回数。 */
static volatile UW gpio_irq_count;

/**
 * @brief GPIO番号が有効範囲内か確認する。
 *
 * @param pin 確認するGPIO番号。
 * @return 0～29なら1、範囲外なら0。
 */
static int gpio_is_valid(unsigned int pin){
    return (pin < 30);
}
/**
 * @brief IO_BANK0とPADS_BANK0のリセットを解除する。
 *
 * RESETS_RESETレジスタの該当ビットをクリアし、両方の回路で
 * リセット解除が完了するまで待つ。GPIOの初期設定前に呼び出す。
 *
 * @note 完了待ちにタイムアウトはない。
 */
static void gpio_reset_release(void){
    UW mask;
    mask = RESETS_RESET_IO_BANK0 | RESETS_RESET_PADS_BANK0;
    /* reset bit をクリア: 該当ブロックのリセット解除 */
    out_w(RESETS_RESET, in_w(RESETS_RESET) & ~mask);
    /* RESET DONEになるまで待つ */
    while((in_w(RESETS_RESET_DONE) & mask) != mask);
}

/**
 * @brief GPIOピンをSIOの出力として初期化する。
 *
 * IO_BANK0とPADS_BANK0のリセットを解除し、指定したピンの機能を
 * SIOに設定して出力を有効にする。
 *
 * @param pin GPIO番号（0～29）。範囲外の場合は何もせずに戻る。
 */
void gpio_init_out(unsigned int pin){
    if(!gpio_is_valid(pin)) return;
    gpio_reset_release();
    /* GPIO pinをSIO機能に接続する */
    out_w(GPIO_CTRL(pin), GPIO_CTRL_FUNCSEL_SIO);
    /* GPIOの出力として有効化 */
    out_w(GPIO_OE_SET, 1U<<pin);
}
/**
 * @brief GPIO出力をHighに設定する。
 *
 * @param pin GPIO番号（0～29）。範囲外の場合は何もせずに戻る。
 */
void gpio_set(unsigned int pin){
    if(!gpio_is_valid(pin)) return;
    out_w(GPIO_OUT_SET, 1U<<pin);
}
/**
 * @brief GPIO出力をLowに設定する。
 *
 * @param pin GPIO番号（0～29）。範囲外の場合は何もせずに戻る。
 */
void gpio_clear(unsigned int pin){
    if(!gpio_is_valid(pin)) return;
    out_w(GPIO_OUT_CLR, 1U<<pin);
}
/**
 * @brief GPIO出力の論理レベルを反転する。
 *
 * 現在の出力がHighならLowに、LowならHighに切り替える。
 *
 * @param pin GPIO番号（0～29）。範囲外の場合は何もせずに戻る。
 */
void gpio_toggle(unsigned int pin){
    if(!gpio_is_valid(pin)) return;
    out_w(GPIO_OUT_XOR, 1U<<pin);
}
/**
 * @brief GPIO出力値を設定する。
 *
 * valueが0以外ならHighに、0ならLowに設定する。
 *
 * @param pin GPIO番号（0～29）。範囲外の場合は何もせずに戻る。
 * @param value 出力値。0でLow、0以外でHigh。
 */
void gpio_put(unsigned int pin, int value){
    if(!gpio_is_valid(pin)) return;
    if(value){
        out_w(GPIO_OUT_SET, 1U<<pin);
    }else{
        out_w(GPIO_OUT_CLR, 1U<<pin);
    }
}
/**
 * @brief GPIOピンの立ち上がりエッジ割り込みを初期化する。
 *
 * 指定したピンを入力に設定し、内部プルダウンを有効にする。
 * GPIOの立ち上がりエッジ割り込みをCore 0で有効にし、
 * 割り込み検出時にnotifyを呼び出す。
 *
 * @param pin 割り込み対象のGPIO番号（0～29）。
 * @param notify 割り込み検出時に呼び出す通知関数。NULLは指定できない。
 * @return 初期化に成功した場合はTRUE、引数が無効な場合はFALSE。
 *
 * @note この関数で設定できる割り込み対象は1ピン。再度呼び出すと対象ピンと通知関数を更新する。
 * @warning notifyは割り込みコンテキストで実行されるため、短い処理にすること。
 */
BOOL gpio_irq_init_rising(unsigned int pin, GPIO_IRQ_NOTIFY_FUNC notify)
{
    UW pad;
    UW shift;
    UW intsts;

    if(!gpio_is_valid(pin) || (notify == NULL)){
        return FALSE;
    }

    gpio_reset_release();
    gpio_irq_pin = pin;
    gpio_irq_group = pin / 8U;
    shift = (pin % 8U) * 4U;
    gpio_irq_mask = 1UL << (shift + 3U);
    gpio_irq_notify = notify;
    gpio_irq_count = 0U;

    out_w(GPIO_CTRL(pin), GPIO_CTRL_FUNCSEL_SIO);
    out_w(GPIO_OE_CLR, 1UL << pin);
    pad = in_w(GPIO(pin));
    pad |= GPIO_IE | GPIO_PDE;
    pad &= ~(GPIO_OD | GPIO_PUE);
    out_w(GPIO(pin), pad);

    DI(intsts);
    out_w(IO_BANK0_INTR(gpio_irq_group), gpio_irq_mask);
    out_w(
        IO_BANK0_PROC0_INTE(gpio_irq_group),
        in_w(IO_BANK0_PROC0_INTE(gpio_irq_group)) | gpio_irq_mask
    );
    out_w(NVIC_ICPR, IO_IRQ_BANK0_MASK);
    out_w(NVIC_ISER, IO_IRQ_BANK0_MASK);
    EI(intsts);

    return TRUE;
}
/**
 * @brief GPIO立ち上がりエッジ割り込みの発生回数を取得する。
 *
 * @return gpio_irq_init_rising()で設定したGPIOピンの割り込み発生回数。
 *
 * @note 割り込み処理で更新されるカウンタを読み出す。
 */
UW gpio_irq_count_get(void)
{
    return gpio_irq_count;
}

/**
 * @brief IO_BANK0割り込みを処理する。
 *
 * 設定済みGPIOピンの割り込み状態を確認する。対象ピンの割り込みが
 * 発生している場合は状態をクリアし、割り込み回数を加算してから
 * 登録された通知関数を呼び出す。
 *
 * @note 割り込みハンドラから呼び出す。通知関数も割り込みコンテキストで実行される。
 */
void io_irq_bank0_handler(void)
{
    UW status;

    status = in_w(IO_BANK0_PROC0_INTS(gpio_irq_group));
    if((status & gpio_irq_mask) != 0U){
        out_w(IO_BANK0_INTR(gpio_irq_group), gpio_irq_mask);
        gpio_irq_count++;
        if(gpio_irq_notify != NULL){
            gpio_irq_notify();
        }
    }
}
/**
 * @brief Pico搭載LED（GPIO25）を出力として初期化する。
 */
void led25_init(void){
    gpio_init_out(PICO_LED_PIN);
}
/**
 * @brief Pico搭載LED（GPIO25）を点灯する。
 */
void led25_on(void){
    gpio_set(PICO_LED_PIN);
}
/**
 * @brief Pico搭載LED（GPIO25）を消灯する。
 */
void led25_off(void){
    gpio_clear(PICO_LED_PIN);
}
/**
 * @brief Pico搭載LED（GPIO25）の点灯状態を反転する。
 */
void led25_toggle(void){
    gpio_toggle(PICO_LED_PIN);
}
/**
 * @brief GPIOピンをI2C機能に設定する。
 *
 * GPIOの機能をI2Cに切り替え、入力と内部プルアップを有効にする。
 * 出力禁止と内部プルダウンは解除する。
 *
 * @param pin GPIO番号（0～29）。範囲外の場合は何もせずに戻る。
 *
 * @note 接続するI2Cコントローラに対応したGPIOピンを指定すること。
 */
void gpio_init_i2c(unsigned int pin)
{
    UW pad;

    if(!gpio_is_valid(pin)) return;

    gpio_reset_release();

    /* GPIOをI2C機能へ接続 */
    out_w(GPIO_CTRL(pin), GPIO_CTRL_FUNCSEL_I2C);

    /*
     * 入力を有効化し、内部プルアップを有効化
     * 出力禁止とプルダウンは解除
     */
    pad = in_w(GPIO(pin));
    pad |= GPIO_IE | GPIO_PUE;
    pad &= ~(GPIO_OD | GPIO_PDE);
    out_w(GPIO(pin), pad);
}
/**
 * @brief GPIOピンをSPI機能に設定する。
 *
 * GPIOの機能をSPIに切り替え、入力を有効にする。
 * オープンドレイン、内部プルアップ、内部プルダウンは無効にする。
 *
 * @param pin GPIO番号（0～29）。範囲外の場合は何もせずに戻る。
 *
 * @note 接続するSPIコントローラに対応したGPIOピンを指定すること。
 */
void gpio_init_spi(unsigned int pin)
{
    UW pad;

    if(!gpio_is_valid(pin)) return;

    gpio_reset_release();
    out_w(GPIO_CTRL(pin), GPIO_CTRL_FUNCSEL_SPI);

    pad = in_w(GPIO(pin));
    pad |= GPIO_IE;
    pad &= ~(GPIO_OD | GPIO_PUE | GPIO_PDE);
    out_w(GPIO(pin), pad);
}
