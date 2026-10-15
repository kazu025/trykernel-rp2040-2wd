/**
 * @file    console.c
 * @brief   UARTコンソールの行入力とコマンド実行を管理する。
 *
 * @details
 * UARTから受信した文字を入力バッファへ蓄積し、改行時にコマンドとして
 * 実行する。バックスペースによる編集、入力文字のエコーバック、
 * バッファ満杯時の通知、およびプロンプト表示も行う。
 * UARTの直接出力は同期制御で保護し、プロンプトや改行文字列は
 * UART送信キューへ登録する。
 *
 */

#include <trykernel.h>
#include "uart.h"
#include "command.h"
#include "console.h"
#include "uart_sync.h"
#include "uart_tx.h"
/* --- 1行入力バッファ --- */
#define CONSOLE_LINE_BUF_SIZE  128  //!< コンソールラインバッファサイズ
static char console_line_buf[CONSOLE_LINE_BUF_SIZE]; //!< コンソールラインバッファ
static UW console_line_pos = 0; //!< バッファ格納位置
/* --- static function --- */
/**
 * @brief コンソールの1行入力状態を初期化する。
 *
 * @details
 * 入力位置を先頭に戻し、コンソール入力バッファの先頭に
 * NULL文字を設定する。
 *
 * @note
 * 入力バッファ全体を消去するのではなく、未入力状態として
 * 先頭位置だけを初期化する。
 */
void console_init(void){
    console_line_pos = 0;
    console_line_buf[0] = '\0';
}
/**
 * @brief コンソールから受信した1文字を処理する。
 *
 * @details
 * 受信文字の種類に応じて、コンソール入力バッファを更新する。
 *
 * - '\r'を受信した場合:
 *   - 入力バッファをNULL終端する。
 *   - command_execute()でコマンドを実行する。
 *   - 入力バッファを初期化する。
 *   - 次のプロンプトを表示する。
 *
 * - バックスペース（'\b'または0x7F）を受信した場合:
 *   - 入力位置を1文字戻す。
 *   - 端末上の表示も1文字消去する。
 *
 * - 通常文字を受信した場合:
 *   - 入力バッファへ格納する。
 *   - 受信文字をエコーバックする。
 *
 * 入力バッファが満杯の場合、追加文字は格納せず、
 * ベル文字（'\a'）を送信する。
 *
 * @param[in] ch 処理する受信文字。
 *
 * @note
 * 入力バッファに格納できる文字数は、
 * NULL終端文字を除いて最大127文字である。
 */
void console_input_char(UB ch){
    if(ch == '\r'){
        console_line_buf[console_line_pos] = '\0'; // Null-terminate the string

        uart_tx_send("\r\n"); // Move to new line
        command_execute(console_line_buf);

        console_line_pos = 0;
        console_line_buf[0] = '\0'; // Clear the line buffer

        console_prompt();
        return;
    }
    /* Backspace */
    if(ch == '\b' || ch == 0x7F){
        if(console_line_pos > 0){
            console_line_pos--;
            uart_sync_lock();
            uart_puts("\b \b"); // Move cursor back, print space, move back again
            uart_sync_unlock();
        }
        return;
    }
    /* Regular character */
    if(console_line_pos < CONSOLE_LINE_BUF_SIZE - 1){
        console_line_buf[console_line_pos++] = (char)ch;
        uart_sync_lock();
        uart_putc(ch);  /* Echo back */
        uart_sync_unlock();
    } else {
        // buffer full, ignore additional characters or handle overflow
        uart_sync_lock();
        uart_putc('\a');  /* Bell character for buffer full */
        uart_sync_unlock();
    }
}
/**
 * @brief コンソールのプロンプトを表示する。
 *
 * @details
 * UART送信キューへプロンプト文字列「> 」を登録する。
 * 実際のUART送信はUART送信タスクによって行われる。
 */
void console_prompt(void){
    uart_tx_send("> ");
}
