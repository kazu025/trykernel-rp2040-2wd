/*
 * UART driver for TryKernel / RP2040 UART0
 * This driver provides polling-based transmission
 * and interrupt-driven reception.
 */
/*
 UART0で受信
  ↓
UART0受信割込み発生
  ↓
uart0_irq_handler()
  ├─ UARTハードウェアFIFOからデータをまとめて読み出す
  ├─ ソフトウェアの受信リングバッファへ格納
  └─ コールバック関数を呼び出す
        受信タスクなどで、コールバック関数を設定する
        コールバック関数は割り込み処理で実行するので、イベント通知等の
        最小限の処理とし、重い処理は受信タスクで実施すること。
 */
#include <trykernel.h>
#include <knldef.h>
#include "uart.h"
/**
 * @brief UARTボーレートの整数分周値。
 *
 * UARTのボーレート設定レジスタへ書き込む整数部。
 * システムクロックと設定対象のボーレートに応じて決定する。
 */
#define UART_BAUD_IBRD  67U

/**
 * @brief UARTボーレートの小数分周値。
 *
 * UARTのボーレート設定レジスタへ書き込む小数部。
 * 小数部0.816に64を乗じ、丸めて52を設定する。
 * (0.816 * 64) + 0.5 = 52.384
 */
#define UART_BAUD_FBRD  52U

/**
 * @brief UART通信形式とFIFOを設定する値。
 *
 * 8データビット、パリティなし、1ストップビット、
 * 送受信FIFO有効を指定する。
 */
#define UART_LCRH_8N1_FIFO  0x70U

/**
 * @brief UART受信リングバッファの容量。
 *
 * 受信したデータを一時的に保持できるバッファ要素数。
 * リングバッファの管理上、実際に格納できる最大データ数は
 * 1要素少ない127バイトになる。
 */
#define UART_RX_BUF_SIZE  128U

/**
 * @brief UART受信リングバッファのインデックス循環用マスク。
 *
 * バッファ容量が2のべき乗であることを利用し、
 * インデックスを0からUART_RX_BUF_SIZE-1の範囲で循環させる。
 */
#define UART_RX_BUF_MASK  (UART_RX_BUF_SIZE - 1U)

/**
 * @brief 受信リングバッファの書き込み位置。
 *
 * UART受信割込み側が、次にデータを書き込む位置を示す。
 */
static volatile UW uart_rx_head = 0U;

/**
 * @brief 受信リングバッファの読み出し位置。
 *
 * UART受信タスク側が、次にデータを読み出す位置を示す。
 */
static volatile UW uart_rx_tail = 0U;

/**
 * @brief ソフトウェア受信リングバッファのオーバーフロー回数。
 *
 * バッファが満杯で、受信データを格納できなかった回数を保持する。
 */
static volatile UW uart_rx_overflow = 0U;

/**
 * @brief UARTハードウェア受信FIFOのオーバーラン回数。
 *
 * UARTハードウェア側で受信FIFOのオーバーランが検出された回数を保持する。
 */
static volatile UW uart_rx_hw_overrun = 0U;

/**
 * @brief UART受信リングバッファ。
 *
 * UART受信割込みでUARTハードウェアFIFOから読み出したデータを保持する。
 */
static volatile UB uart_rx_buf[UART_RX_BUF_SIZE];

/**
 * @brief UART受信時に呼び出す通知関数。
 *
 * 受信データがリングバッファへ格納されたときに、
 * UART受信割込みコンテキストから呼び出される。
 */
static UART_RX_NOTIFY_FUNC uart_rx_notify = NULL;

/**
 * @brief UART受信FIFO割込みの発生回数。
 */
static volatile UW uart_rx_irq_count = 0U;

/**
 * @brief UART受信タイムアウト割込みの発生回数。
 */
static volatile UW uart_rt_irq_count = 0U;

static int uart_rxbuf_put(UB data);
/* ---------------------------------------------------------- */
/**
 * @brief UART受信FIFO割込みの発生回数を取得する。
 *
 * @details
 * UART受信FIFO割込み（RXIM）が発生した回数を返す。
 *
 * @return UW UART受信FIFO割込みの発生回数。
 */
UW uart_rx_irq_count_get(void){
    return uart_rx_irq_count;
}
/**
 * @brief UART受信タイムアウト割込みの発生回数を取得する。
 *
 * @details
 * UART受信タイムアウト割込み（RTIM）が発生した回数を返す。
 *
 * @return UW UART受信タイムアウト割込みの発生回数。
 */
UW uart_rt_irq_count_get(void){
    return uart_rt_irq_count;
}
/**
 * @brief UART0を初期化する。
 *
 * @details
 * ボーレート、8ビット・パリティなし・1ストップビットの通信形式、
 * FIFOの使用を設定し、UART0の送信と受信を有効にする。
 *
 * @note この関数では受信割込みは有効にしない。
 *       受信割込みは uart_rx_irq_enable() で有効にする。
 */
void uart_init(void){
    out_w(UART0_BASE + UARTx_IBRD, UART_BAUD_IBRD);                         /* ボーレート設定 */
    out_w(UART0_BASE + UARTx_FBRD, UART_BAUD_FBRD);
    out_w(UART0_BASE + UARTx_LCR_H, UART_LCRH_8N1_FIFO);                    /* データ形式設定 */
    out_w(UART0_BASE + UARTx_CR, UART_CR_RXE | UART_CR_TXE | UART_CR_EN);   /* 通信イネーブル */
}
/**
 * @brief UART受信リングバッファの管理情報を初期化する。
 *
 * @details
 * 読み書き位置を先頭に戻し、バッファのオーバーフロー回数、
 * ハードウェアのオーバーラン回数、受信割込み回数、
 * 受信タイムアウト割込み回数を0にする。
 *
 * @note バッファ内のデータそのものは消去しない。
 */
void uart_rxbuf_init(void){
    uart_rx_head = 0;
    uart_rx_tail = 0;
    uart_rx_overflow = 0;
    uart_rx_hw_overrun = 0;
    uart_rx_irq_count = 0;
    uart_rt_irq_count = 0;
}
/**
 * @brief UART0の受信割込みを有効にする。
 *
 * @details
 * 割込み禁止中にUART0側の割込みを一度マスクし、UART0とNVICに
 * 残っている割込み要求をクリアする。その後、受信FIFOの
 * しきい値到達、受信タイムアウト、オーバーランの各割込みを
 * UART0側で許可し、NVICのUART0割込み（IRQ20）を有効にする。
 * 最後に、この関数を呼ぶ前の割込み許可状態へ戻す。
 */
void uart_rx_irq_enable(void){
    UW intsts;
    DI(intsts);
    /* UART側の割り込みをすべてマスク */
    out_w(UART0_BASE + UARTx_IMSC, 0U);
    /* UART0の保留中割り込みをクリアする */
    out_w(UART0_BASE + UARTx_ICR,
        UART_ICR_RXIC | UART_ICR_RTIC | UART_ICR_OEIC);
    out_w(NVIC_ICPR, UART0_IRQ_MASK);
    /*
     * RXIM : RX FIFOがしきい値に達したとき
     * RTIM : RX FIFOにデータが残ったまま32ビット期間受信がないとき
     * OEIM : RX FIFOのオーバーランが発生したとき
     */
    out_w(UART0_BASE + UARTx_IMSC,
        UART_IMSC_RXIM | UART_IMSC_RTIM | UART_IMSC_OEIM);
    /* NVICでUART0 IRQ(IRQ20) を有効化する */
    out_w(NVIC_ISER, UART0_IRQ_MASK);
    EI(intsts);
}
/**
 * @brief UART送信FIFOに空きがあるか確認する。
 *
 * @retval TRUE 送信FIFOに空きがあり、データを書き込める。
 * @retval FALSE 送信FIFOが満杯で、データを書き込めない。
 */
BOOL uart_can_send(void){
    return (in_w(UART0_BASE + UARTx_FR) & UART_FR_TXFF) == 0;
}
/**
 * @brief UART受信FIFOにデータがあるか確認する。
 *
 * @retval TRUE 受信FIFOにデータがある。
 * @retval FALSE 受信FIFOが空である。
 */
BOOL uart_can_recv(void){
    return (in_w(UART0_BASE + UARTx_FR) & UART_FR_RXFE) == 0;
}
/**
 * @brief UARTから1バイト送信する。
 *
 * @details
 * 送信FIFOに空きができるまで待ち、データレジスタに
 * 1バイトを書き込む。
 *
 * @param[in] c 送信するデータ。
 *
 * @note FIFOの空き待ちはポーリングで行うため、その間は呼び出し元が停止する。
 */
void uart_putc(UB c){
    while(!uart_can_send());
    out_w(UART0_BASE + UARTx_DR, ((UW)c) & 0xFFU);
}
/**
 * @brief NULL終端文字列をUARTから送信する。
 *
 * @details
 * 文字列の終端文字（'\0'）まで1文字ずつ送信する。
 * 各文字の送信は、送信FIFOに空きができるまで待ってから行う。
 *
 * @param[in] s 送信するNULL終端文字列。
 *
 * @return UINT
 * @retval 0 文字列がNULL、または送信文字数が0。
 * @retval その他 実際に送信した文字数。
 *
 * @note NULLが指定された場合は何も送信せず、0を返す。
 */
UINT uart_puts(const char *s){
    UINT count = 0;
    if(s == NULL) return 0;
    while(*s != '\0'){
        uart_putc((UB)(*s++));
        count++;
    }
    return count;
}
/**
 * @brief UARTから1バイトをブロッキングせずに受信する。
 *
 * @details
 * 受信FIFOにデータがある場合だけデータレジスタから1バイトを読み出す。
 * データがない場合は待機せず、直ちにFALSEを返す。
 *
 * @param[out] c 受信した1バイトを格納する領域。
 *
 * @retval TRUE 1バイトの受信に成功した。
 * @retval FALSE 受信データがない、またはcがNULL。
 */
BOOL uart_getc_nonblock(UB *c){
    if(c == NULL) return FALSE;
    if(uart_can_recv()){
        *c = (UB)(in_w(UART0_BASE + UARTx_DR) & 0xFFU);
        return TRUE;
    }
    return FALSE;
}
/**
 * @brief UARTから1バイトを受信する。
 *
 * @details
 * 受信FIFOにデータが届くまで、uart_getc_nonblock()を繰り返し呼び出して待機する。
 * データを受信すると、その1バイトを返す。
 *
 * @return UB 受信した1バイトのデータ。
 *
 * @note 受信データが到着するまでポーリングによる待機を続けるため、
 *       呼び出し元はその間ブロックされる。
 */
UB uart_getc(void){
    UB c;
    while(!uart_getc_nonblock(&c));
    return c;
}
/**
 * @brief UART受信リングバッファのオーバーフロー回数を取得する。
 *
 * @details
 * 受信リングバッファが満杯で、受信データを格納できなかった回数を返す。
 *
 * @return UW オーバーフロー発生回数。
 */
UW uart_rx_overflow_count(void){
    return uart_rx_overflow;
}
/**
 * @brief UART受信ハードウェアオーバーランの発生回数を取得する。
 *
 * @details
 * UARTハードウェアの受信FIFOでオーバーランが検出された回数を返す。
 * オーバーランは、FIFOの処理が間に合わず受信データが失われた場合に発生する。
 *
 * @return UW ハードウェアオーバーランの発生回数。
 */
UW uart_rx_hw_overrun_count(void){
    return uart_rx_hw_overrun;
}
/**
 * @brief UART受信リングバッファから1バイト取り出す。
 *
 * @details
 * 受信リングバッファが空でない場合、読み出し位置のデータを
 * dataへ格納し、読み出し位置を次へ進める。
 * データがない場合は待機せず、直ちに失敗を返す。
 *
 * @param[out] data 取り出した受信データを格納する領域。
 *
 * @retval 1 受信データの取り出しに成功した。
 * @retval 0 バッファが空、またはdataがNULL。
 */
int uart_rx_getc(UB *data){
    if(data == NULL) return 0;
    if(uart_rx_head == uart_rx_tail){
        return 0; // buffer empty
    }
    *data = uart_rx_buf[uart_rx_tail];
    uart_rx_tail = (uart_rx_tail + 1) & UART_RX_BUF_MASK;
    return 1; // success
}
/**
 * @brief UART0の受信割込みを処理する。
 *
 * @details
 * UARTの受信FIFOにあるデータをすべて読み出し、ソフトウェアの
 * 受信リングバッファへ格納する。ハードウェアのオーバーランを
 * 検出した場合は回数を記録し、受信割込み要因をクリアする。
 *
 * 1文字以上をリングバッファに格納できた場合、登録済みの
 * 通知関数を割込みコンテキストから呼び出す。
 *
 * @note 通知関数では、待ちを伴う処理を行わないこと。
 */
void uart0_irq_handler(void){
    UB data;    // 受信データ/
    UW mis;     // 割り込み要因
    UW status;  // 受信エラー状態
    UW clear = 0U; // クリアする割り込み要因
    BOOL received = FALSE;

    mis = in_w(UART0_BASE + UARTx_MIS); // Masked Interrupt Status Register
    status = in_w(UART0_BASE + UARTx_RSR_ECR); // Receive Status Register
    /* HW overrun　検出 */
    if(((mis & UART_MIS_OEMIS) != 0U) || ((status & UART_RSR_OE) != 0U)){
        uart_rx_hw_overrun++;
        /* error clear */
        out_w(UART0_BASE + UARTx_RSR_ECR, 0U);
        clear |= UART_ICR_OEIC;
    }
    /* FIFO全データ読み出し */
    while(uart_can_recv()){
        data = (UB)(in_w(UART0_BASE + UARTx_DR) & 0xFFU);
        if(uart_rxbuf_put(data)){
            received = TRUE;
        }
    }
    if((mis & UART_MIS_RXMIS) != 0U){
        uart_rx_irq_count++;
        /* RX FIFO 割り込み要因クリア */
        clear |= UART_ICR_RXIC;
    }
    if((mis & UART_MIS_RTMIS) != 0U){
        uart_rt_irq_count++;
        /* 受信タイムアウト割り込み要因クリア */
        clear |= UART_ICR_RTIC;
    }
    if(clear != 0U){
        /* 割り込み要因をクリア */
        out_w(UART0_BASE + UARTx_ICR, clear);
    }
    /*
     * 割り込みコンテキストから通知する
     * 登録する関数では待ち処理などを行わないこと
     */
    if((received != FALSE) && (uart_rx_notify != NULL)){
        uart_rx_notify();
    }
}
/**
 * @brief 受信した1バイトをソフトウェアのリングバッファに格納する。
 *
 * @details
 * 書き込み位置の次が読み出し位置に一致する場合は、バッファが満杯と
 * 判断する。その場合、データは格納せず、オーバーフロー回数を増やす。
 *
 * @param[in] data 格納する受信データ。
 *
 * @retval 1 格納に成功した。
 * @retval 0 バッファが満杯で格納できなかった。
 */
static int uart_rxbuf_put(UB data){
    UW next;
    next = (uart_rx_head + 1) & UART_RX_BUF_MASK;
    if(next == uart_rx_tail){
        uart_rx_overflow++;
        return 0; // buffer full
    }
    uart_rx_buf[uart_rx_head] = data;
    uart_rx_head = next;
    return 1; // success
}
/**
 * @brief UART受信時に呼び出す通知関数を登録する。
 *
 * @details
 * 割込みハンドラから参照される関数ポインタを、割込み禁止中に更新する。
 * NULLを指定すると通知を無効にする。
 *
 * @param[in] notify 登録する通知関数。通知を無効にする場合はNULL。
 *
 * @note 通知関数は割込みコンテキストで呼び出されるため、
 *       待ちを伴う処理を行わないこと。
 */
void uart_rx_set_notify(UART_RX_NOTIFY_FUNC notify){
    UW intsts;
    DI(intsts);
    uart_rx_notify = notify;
    EI(intsts);
}

