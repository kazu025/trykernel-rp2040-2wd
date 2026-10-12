#ifndef UART_TX_INTERNAL_H
#define UART_TX_INTERNAL_H
/* -------------------------------------------------- */
#include <trykernel.h>
#define UART_TX_MESSAGE_SIZE  128U  //!< 送信メッセージの最大長

/* 非公開関数 */
ER uart_tx_wait(void);
ER uart_tx_receive(char *text);
/* -------------------------------------------------- */
#endif /* UART_TX_INTERNAL_H */