/**
 * @file    mini_printf.c
 * @brief   小規模な書式付き文字列生成機能を実装する。
 *
 * @details
 * 標準ライブラリのprintf系関数に依存せず、指定された出力バッファへ
 * 書式付き文字列を生成する。文字列、文字、符号付き10進数、符号なし
 * 10進数、16進数、およびパーセント記号の書式指定に対応する。
 * バッファが不足する場合は格納可能な範囲まで出力し、必要な出力文字数を
 * 戻り値で通知する。
 *
 */

#include <trykernel.h>
#include <stdarg.h>

#include "mini_printf.h"

/*
 * 出力先管理
 */
typedef struct {
    char *buf;  /* 出力バッファ */
    UW size;    /* バッファサイズ */
    UW pos;     /* 実際に格納した文字数*/
    UW total;   /* 本来出力される文字数 */
} MINI_OUTPUT;

/* 内部関数 */
static void mini_output_char(MINI_OUTPUT* out, char ch);
static void mini_output_string(MINI_OUTPUT* out, const char *text);
static void mini_output_unsigned(MINI_OUTPUT* out, UINT value, UINT base);
static void mini_output_signed(MINI_OUTPUT* out, INT value);


/**
 * @brief 1文字を出力状態へ追加する。
 * @param[in,out] out 出力先バッファと文字数を管理する状態。
 * @param[in] ch 追加する文字。
 * @details バッファに空きがあれば終端文字用の領域を残して格納し、
 *          格納の可否にかかわらず必要な出力文字数を加算する。
 */
static void mini_output_char(MINI_OUTPUT* out, char ch){
    /*
     * 終端文字用の１バイトを残して格納する。
     * バッファが満杯の場合もtotalは増加させる
     */
    if((out->buf != NULL) && (out->size > 0U) && (out->pos < (out->size - 1U))) {
        out->buf[out->pos] = ch;
        out->pos++;
    }
    out->total++;
}
/**
 * @brief 文字列を出力状態へ追加する。
 * @param[in,out] out 出力先バッファと文字数を管理する状態。
 * @param[in] text 追加する文字列。NULLの場合は "(null)" として出力する。
 */
static void mini_output_string(MINI_OUTPUT* out, const char *text){
    if (text == NULL){
        text = "(null)";
    }
    while(*text != '\0'){
        mini_output_char(out, *text);
        text++;
     }
}

/**
 * @brief 符号なし整数を指定した基数で出力状態へ追加する。
 * @param[in,out] out 出力先バッファと文字数を管理する状態。
 * @param[in] value 変換する整数。
 * @param[in] base 使用する基数（呼び出し元では10または16）。
 * @details 数字は小文字の16進数字を用い、桁の順序を整えて出力する。
 */
static void mini_output_unsigned(MINI_OUTPUT* out, UINT value, UINT base){
    static const char digits[] = "0123456789abcdef";
    char work[32];
    UW count = 0U;
    /* 0は特別処理 */
    if(value == 0){
        mini_output_char(out, '0');
        return;
    }
    /* 下位桁からworkへ */
    while((value != 0) && (count < (UW)sizeof(work))){
        work[count] = digits[value % base];
        count ++;
        value /= base;
    }
    /* 逆順で出力 */
    while(count > 0U){
        count--;
        mini_output_char(out, work[count]);
    }
}
/**
 * @brief 符号付き整数を10進数として出力状態へ追加する。
 * @param[in,out] out 出力先バッファと文字数を管理する状態。
 * @param[in] value 変換する整数。
 * @details INTの最小値も安全に絶対値へ変換できるように処理する。
 */
static void mini_output_signed(MINI_OUTPUT* out, INT value){
    UINT magnitude;
    if(value < 0){
        mini_output_char(out,'-');
        /*
         * INT最小値でもオーバーフローしないように
         * -(value + 1)の後で1を加える
         */
        magnitude =(UINT)(-(value + 1));
        magnitude++;
    } else {
        magnitude = (UINT)value;
    }
    mini_output_unsigned(out, magnitude, 10U);
}
/**
 * @brief va_listを使って書式付き文字列をバッファへ生成する。
 * @param[out] buf 出力先バッファ。sizeが0の場合はNULLを指定できる。
 * @param[in] size 出力先バッファのサイズ（終端文字を含む）。
 * @param[in] format 書式文字列。%s、%c、%d、%u、%x、%%に対応する。
 * @param[in] args 書式指定子に対応する可変長引数リスト。
 * @return 終端文字を除く、バッファサイズにかかわらず生成しようとした文字数。
 * @retval E_PAR formatがNULL、またはbufがNULLかつsizeが0より大きい。
 * @details sizeが0より大きい場合、出力はバッファに収まる範囲へ切り詰め、
 *          必ずNULL終端する。未対応の書式指定子は「%」と指定文字をそのまま出力する。
 */
INT mini_vsnprintf(char *buf, UW size, const char *format, va_list args){
    MINI_OUTPUT out;
    if(format == NULL){
        return E_PAR;
    }
    if ((buf == NULL) && (size > 0U)){
        return E_PAR;
    }

    out.buf     = buf;
    out.size    = size;
    out.pos     = 0U;
    out.total   = 0U;

    while(*format != '\0'){
        /* 通常文字 */
        if(*format != '%'){
            mini_output_char(&out, *format);
            format++;
            continue;
        }
        /* '%'の次の文字を調べる */
        format++;
        if (*format == '\0'){
            mini_output_char(&out, '%');
            break;
        }

        switch(*format){
        case 's':
            mini_output_string(&out, va_arg(args, const char*));
            break;
        case 'c':
            /* char は可変長引数ではintへ整数拡張される */
            mini_output_char(&out, (char)va_arg(args, int));
            break;
        case 'd':
            mini_output_signed(&out, (INT)va_arg(args, int));
            break;
        case 'u':
            mini_output_unsigned(&out, (UINT)va_arg(args, unsigned int), 10U);
            break;
        case 'x':
            mini_output_unsigned(&out, (UINT)va_arg(args, unsigned int), 16U);
            break;
        case '%':
            mini_output_char(&out, '%');
            break;
        default:
            /* 未対応の指定はそのまま表示する */
            mini_output_char(&out, '%');
            mini_output_char(&out, *format);
            break;
        }
        format++;
    }
    /* 文字列終端を保証する */
    if((buf != NULL) && (size > 0 )){
        buf[out.pos] = '\0';
    }
    return (INT)out.total;
}

/**
 * @brief 可変長引数を受け取り、書式付き文字列をバッファへ生成する。
 * @param[out] buf 出力先バッファ。sizeが0の場合はNULLを指定できる。
 * @param[in] size 出力先バッファのサイズ（終端文字を含む）。
 * @param[in] format 書式文字列。%s、%c、%d、%u、%x、%%に対応する。
 * @param[in] ... 書式指定子に対応する可変長引数。
 * @return 終端文字を除く、バッファサイズにかかわらず生成しようとした文字数。
 * @retval E_PAR formatがNULL、またはbufがNULLかつsizeが0より大きい。
 * @details 出力処理はmini_vsnprintf()へ委譲する。
 */
INT mini_snprintf(char* buf, UW size, const char* format, ...){
    va_list args;
    INT result;

    va_start(args, format);
    result = mini_vsnprintf(buf, size, format, args);
    va_end(args);
    return result;
}
