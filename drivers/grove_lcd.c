/**
 * @file grove_lcd.c
 * @brief Grove LCD RGB Backlight V5.0を制御するI2Cドライバー。
 *
 * LCDへの文字表示とカーソル制御、画面クリア、RGBバックライトの
 * 色設定を行う。LCDとバックライトのI2C通信はセマフォで排他制御する。
 */
#include <trykernel.h>
#include "i2c.h"
#include "grove_lcd.h"

/** @brief Grove LCDコントローラーのI2Cアドレス（7ビット）。 */
#define GROVE_LCD_ADDRESS        0x3EU

/** @brief Grove RGBバックライトコントローラー V5.0のI2Cアドレス（7ビット）。 */
#define GROVE_RGB_ADDRESS_V5     0x30U

/** @brief LCDへコマンドを送信するときの制御バイト。 */
#define GROVE_LCD_CONTROL_CMD    0x80U

/** @brief LCDへ表示データを送信するときの制御バイト。 */
#define GROVE_LCD_CONTROL_DATA   0x40U

/** @brief LCDの表示内容を消去し、カーソルを先頭に戻すコマンド。 */
#define GROVE_LCD_CMD_CLEAR      0x01U

/** @brief 文字入力後にカーソルを左から右へ進める設定コマンド。 */
#define GROVE_LCD_CMD_ENTRY_MODE 0x06U

/** @brief LCD表示を有効にし、カーソルとカーソル点滅を無効にするコマンド。 */
#define GROVE_LCD_CMD_DISPLAY_ON 0x0CU

/** @brief 2行表示、5×8ドット文字、8ビットインターフェースを設定するコマンド。 */
#define GROVE_LCD_CMD_FUNCTION   0x28U

/** @brief RGBバックライトコントローラーのリセットレジスター。 */
#define GROVE_RGB_REG_RESET      0x00U

/** @brief RGB出力の有効・無効を設定するレジスター。 */
#define GROVE_RGB_REG_LEDOUT     0x04U

/** @brief バックライトの赤色成分を設定するレジスター。 */
#define GROVE_RGB_REG_RED        0x06U

/** @brief バックライトの緑色成分を設定するレジスター。 */
#define GROVE_RGB_REG_GREEN      0x07U

/** @brief バックライトの青色成分を設定するレジスター。 */
#define GROVE_RGB_REG_BLUE       0x08U

static ID grove_lcd_semid;
/**
 * @brief LCDドライバーのセマフォを取得する。
 *
 * セマフォが未作成の場合はFALSEを返す。作成済みの場合は、
 * セマフォを取得できるまで待つ。
 *
 * @return セマフォを取得できた場合はTRUE、未作成または取得に失敗した場合はFALSE。
 */
static BOOL grove_lcd_lock(void)
{
    if(grove_lcd_semid <= 0){
        return FALSE;
    }

    return (tk_wai_sem(grove_lcd_semid, 1, TMO_FEVR) == E_OK);
}
/**
 * @brief LCDドライバーのセマフォを解放する。
 *
 * @return セマフォの解放に成功した場合はTRUE、失敗した場合はFALSE。
 */
static BOOL grove_lcd_unlock(void)
{
    return (tk_sig_sem(grove_lcd_semid, 1) == E_OK);
}
/**
 * @brief Grove LCDドライバーの排他制御用セマフォを生成する。
 *
 * 初期カウントと最大カウントを1に設定する。
 *
 * @retval E_OK セマフォを生成した。
 * @retval その他 tk_cre_sem()が返したエラーコード。
 */
ER grove_lcd_sync_init(void)
{
    T_CSEM csem;

    csem.sematr = TA_TFIFO | TA_FIRST;
    csem.isemcnt = 1;
    csem.maxsem = 1;

    grove_lcd_semid = tk_cre_sem(&csem);
    if(grove_lcd_semid < E_OK){
        return (ER)grove_lcd_semid;
    }

    return E_OK;
}

/**
 * @brief I2Cデバイスへ2バイトのデータを送信する。
 *
 * firstとsecondを連続した2バイトとして送信する。
 *
 * @param address I2Cデバイスの7ビットアドレス。
 * @param first 送信する1バイト目。
 * @param second 送信する2バイト目。
 * @return I2C送信に成功した場合はTRUE、失敗した場合はFALSE。
 */
static BOOL grove_lcd_write_pair(UB address, UB first, UB second)
{
    UB data[2];

    data[0] = first;
    data[1] = second;

    return i2c0_write(address, data, 2U);
}
/**
 * @brief LCDへコマンドを送信する。
 *
 * LCDコントローラーのI2Cアドレスとコマンド用制御バイトを使って、
 * 指定されたコマンドを送信する。
 *
 * @param command LCDへ送信するコマンド。
 * @return 送信に成功した場合はTRUE、失敗した場合はFALSE。
 *
 * @note 呼び出し元でLCDドライバーのセマフォを取得しておくこと。
 */
static BOOL grove_lcd_command_unlocked(UB command)
{
    return grove_lcd_write_pair(
        GROVE_LCD_ADDRESS,
        GROVE_LCD_CONTROL_CMD,
        command
    );
}
/**
 * @brief RGBバックライトコントローラーのレジスターへ値を書き込む。
 *
 * @param reg 書き込み先レジスターアドレス。
 * @param value レジスターに書き込む値。
 * @return I2C送信に成功した場合はTRUE、失敗した場合はFALSE。
 *
 * @note 呼び出し元でLCDドライバーのセマフォを取得しておくこと。
 */
static BOOL grove_rgb_write_register_unlocked(UB reg, UB value)
{
    return grove_lcd_write_pair(
        GROVE_RGB_ADDRESS_V5,
        reg,
        value
    );
}
/**
 * @brief LCD画面を消去し、カーソルを先頭へ戻す。
 *
 * 画面クリアコマンドを送信し、処理完了まで10ミリ秒待つ。
 *
 * @return コマンド送信に成功した場合はTRUE、失敗した場合はFALSE。
 *
 * @note 呼び出し元でLCDドライバーのセマフォを取得しておくこと。
 */
static BOOL grove_lcd_clear_unlocked(void)
{
    if(grove_lcd_command_unlocked(GROVE_LCD_CMD_CLEAR) == FALSE){
        return FALSE;
    }

    tk_dly_tsk(10);
    return TRUE;
}
/**
 * @brief LCDカーソルの位置を設定する。
 *
 * 指定した列と行からLCDの表示アドレスを計算し、カーソル位置を変更する。
 *
 * @param column 列番号（0～GROVE_LCD_COLUMNS - 1）。
 * @param row 行番号（0～GROVE_LCD_ROWS - 1）。
 * @return 有効な位置へのコマンド送信に成功した場合はTRUE。それ以外はFALSE。
 *
 * @note 呼び出し元でLCDドライバーのセマフォを取得しておくこと。
 */
static BOOL grove_lcd_set_cursor_unlocked(UB column, UB row)
{
    UB address;

    if((column >= GROVE_LCD_COLUMNS) || (row >= GROVE_LCD_ROWS)){
        return FALSE;
    }

    address = (row == 0U)
        ? (UB)(0x80U + column)
        : (UB)(0xC0U + column);

    return grove_lcd_command_unlocked(address);
}
/**
 * @brief LCDにNULL終端文字列を表示する。
 *
 * 文字列の各文字を表示データとして順に送信する。
 *
 * @param text 表示するNULL終端文字列。NULLの場合はFALSEを返す。
 * @return すべての文字を送信できた場合はTRUE、I2C送信に失敗した場合はFALSE。
 *
 * @note 表示位置は現在のカーソル位置から始まる。
 * @note 呼び出し元でLCDドライバーのセマフォを取得しておくこと。
 */
static BOOL grove_lcd_write_text_unlocked(const char *text)
{
    if(text == NULL){
        return FALSE;
    }

    while(*text != '\0'){
        if(grove_lcd_write_pair(
                GROVE_LCD_ADDRESS,
                GROVE_LCD_CONTROL_DATA,
                (UB)*text) == FALSE){
            return FALSE;
        }
        text++;
    }

    return TRUE;
}
/**
 * @brief RGBバックライトを初期化し、指定した色に設定する。
 *
 * RGBコントローラーをリセットして出力を有効にした後、
 * 赤・緑・青の各輝度値を書き込む。
 *
 * @param red 赤色成分（0～255）。
 * @param green 緑色成分（0～255）。
 * @param blue 青色成分（0～255）。
 * @return すべてのI2C書き込みに成功した場合はTRUE、失敗した場合はFALSE。
 *
 * @note 呼び出し元でLCDドライバーのセマフォを取得しておくこと。
 */
static BOOL grove_lcd_set_rgb_unlocked(UB red, UB green, UB blue)
{
    /* 単独で呼び出しても動作するようRGBドライバを初期化する */
    if(grove_rgb_write_register_unlocked(
            GROVE_RGB_REG_RESET, 0x07U) == FALSE){
        return FALSE;
    }
    tk_dly_tsk(10);

    if(grove_rgb_write_register_unlocked(
            GROVE_RGB_REG_LEDOUT, 0x15U) == FALSE){
        return FALSE;
    }

    if(grove_rgb_write_register_unlocked(GROVE_RGB_REG_RED, red) == FALSE){
        return FALSE;
    }
    if(grove_rgb_write_register_unlocked(
            GROVE_RGB_REG_GREEN, green) == FALSE){
        return FALSE;
    }
    if(grove_rgb_write_register_unlocked(GROVE_RGB_REG_BLUE, blue) == FALSE){
        return FALSE;
    }

    return TRUE;
}
/**
 * @brief LCDとRGBバックライトを初期化する。
 *
 * 電源投入後に待機し、LCDを初期化して表示を有効にする。
 * 画面を消去してカーソルの移動方向を設定した後、
 * RGBバックライトを初期色（赤64、緑128、青255）に設定する。
 *
 * @return 初期化中のすべてのI2C通信に成功した場合はTRUE、失敗した場合はFALSE。
 *
 * @note 呼び出し元でLCDドライバーのセマフォを取得しておくこと。
 */
static BOOL grove_lcd_init_unlocked(void)
{
    /* LCD電源投入後の待ち時間 */
    tk_dly_tsk(50);

    if(grove_lcd_command_unlocked(GROVE_LCD_CMD_FUNCTION) == FALSE){
        return FALSE;
    }
    tk_dly_tsk(10);

    if(grove_lcd_command_unlocked(GROVE_LCD_CMD_FUNCTION) == FALSE){
        return FALSE;
    }
    tk_dly_tsk(10);

    if(grove_lcd_command_unlocked(GROVE_LCD_CMD_FUNCTION) == FALSE){
        return FALSE;
    }
    if(grove_lcd_command_unlocked(GROVE_LCD_CMD_FUNCTION) == FALSE){
        return FALSE;
    }
    if(grove_lcd_command_unlocked(GROVE_LCD_CMD_DISPLAY_ON) == FALSE){
        return FALSE;
    }
    if(grove_lcd_clear_unlocked() == FALSE){
        return FALSE;
    }
    if(grove_lcd_command_unlocked(GROVE_LCD_CMD_ENTRY_MODE) == FALSE){
        return FALSE;
    }

    return grove_lcd_set_rgb_unlocked(64U, 128U, 255U);
}
/**
 * @brief Grove LCDとRGBバックライトを初期化する。
 *
 * セマフォを取得して初期化処理を実行し、完了後にセマフォを解放する。
 *
 * @return 初期化とセマフォ解放に成功した場合はTRUE、いずれかに失敗した場合はFALSE。
 *
 * @note 事前にgrove_lcd_sync_init()を呼び出してセマフォを生成しておくこと。
 */
BOOL grove_lcd_init(void)
{
    BOOL result;

    if(grove_lcd_lock() == FALSE){
        return FALSE;
    }
    result = grove_lcd_init_unlocked();
    if(grove_lcd_unlock() == FALSE){
        return FALSE;
    }
    return result;
}
/**
 * @brief LCD画面を消去し、カーソルを先頭に戻す。
 *
 * セマフォを取得して画面クリアを実行し、完了後にセマフォを解放する。
 *
 * @return 画面クリアとセマフォ解放に成功した場合はTRUE、いずれかに失敗した場合はFALSE。
 *
 * @note 事前にgrove_lcd_sync_init()を呼び出してセマフォを生成しておくこと。
 */
BOOL grove_lcd_clear(void)
{
    BOOL result;

    if(grove_lcd_lock() == FALSE){
        return FALSE;
    }
    result = grove_lcd_clear_unlocked();
    if(grove_lcd_unlock() == FALSE){
        return FALSE;
    }
    return result;
}
/**
 * @brief LCDのカーソル位置を設定する。
 *
 * セマフォを取得して指定位置への変更を行い、完了後に解放する。
 *
 * @param column 列番号（0～GROVE_LCD_COLUMNS - 1）。
 * @param row 行番号（0～GROVE_LCD_ROWS - 1）。
 * @return 位置が有効で、カーソル設定とセマフォ解放に成功した場合はTRUE。
 *         位置が範囲外、または処理に失敗した場合はFALSE。
 *
 * @note 事前にgrove_lcd_sync_init()を呼び出してセマフォを生成しておくこと。
 */
BOOL grove_lcd_set_cursor(UB column, UB row)
{
    BOOL result;

    if(grove_lcd_lock() == FALSE){
        return FALSE;
    }
    result = grove_lcd_set_cursor_unlocked(column, row);
    if(grove_lcd_unlock() == FALSE){
        return FALSE;
    }
    return result;
}
/**
 * @brief LCDの現在のカーソル位置から文字列を表示する。
 *
 * セマフォを取得して文字列を送信し、完了後にセマフォを解放する。
 *
 * @param text 表示するNULL終端文字列。NULLの場合はFALSEを返す。
 * @return 文字列の送信とセマフォ解放に成功した場合はTRUE、
 *         いずれかに失敗した場合はFALSE。
 *
 * @note 事前にgrove_lcd_sync_init()を呼び出してセマフォを生成しておくこと。
 */
BOOL grove_lcd_write_text(const char *text)
{
    BOOL result;

    if(grove_lcd_lock() == FALSE){
        return FALSE;
    }
    result = grove_lcd_write_text_unlocked(text);
    if(grove_lcd_unlock() == FALSE){
        return FALSE;
    }
    return result;
}
/**
 * @brief LCDの1行目と2行目に文字列を表示する。
 *
 * 各行の先頭にカーソルを移動して文字列を書き込む。
 * 2行の更新中はセマフォを保持する。
 *
 * @param first_line 1行目に表示するNULL終端文字列。
 * @param second_line 2行目に表示するNULL終端文字列。
 * @return 両方の文字列の送信とセマフォ解放に成功した場合はTRUE。
 *         引数がNULL、または処理に失敗した場合はFALSE。
 *
 * @note 事前にgrove_lcd_sync_init()を呼び出してセマフォを生成しておくこと。
 * @note 各行の表示幅を超える文字列の切り詰めは行わない。
 */
BOOL grove_lcd_update_lines(const char *first_line, const char *second_line)
{
    BOOL result;

    if((first_line == NULL) || (second_line == NULL)){
        return FALSE;
    }
    if(grove_lcd_lock() == FALSE){
        return FALSE;
    }

    result = grove_lcd_set_cursor_unlocked(0U, 0U)
        && grove_lcd_write_text_unlocked(first_line)
        && grove_lcd_set_cursor_unlocked(0U, 1U)
        && grove_lcd_write_text_unlocked(second_line);

    if(grove_lcd_unlock() == FALSE){
        return FALSE;
    }
    return result;
}
/**
 * @brief RGBバックライトの色を設定する。
 *
 * セマフォを取得してRGBコントローラーを初期化し、
 * 指定した色を設定した後にセマフォを解放する。
 *
 * @param red 赤色成分（0～255）。
 * @param green 緑色成分（0～255）。
 * @param blue 青色成分（0～255）。
 * @return 色の設定とセマフォ解放に成功した場合はTRUE、
 *         いずれかに失敗した場合はFALSE。
 *
 * @note 事前にgrove_lcd_sync_init()を呼び出してセマフォを生成しておくこと。
 */
BOOL grove_lcd_set_rgb(UB red, UB green, UB blue)
{
    BOOL result;

    if(grove_lcd_lock() == FALSE){
        return FALSE;
    }
    result = grove_lcd_set_rgb_unlocked(red, green, blue);
    if(grove_lcd_unlock() == FALSE){
        return FALSE;
    }
    return result;
}
