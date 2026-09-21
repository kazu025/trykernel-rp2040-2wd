
#ifndef SYSDEF_H
#define SYSDEF_H
/*
 *** Try Kernel
 *          システム（ハードウェア）関連共通定義
 */

/* メモリ関連 */
#define SRAM_START              (0x20000000)
#define SRAM_SIZE               (256*1024)

#define	INITIAL_SP              (SRAM_START + SRAM_SIZE)

/* APB ペリフェラル */
/* Clocks */
#define CLOCKS_BASE             0x40008000
#define CLK_GPOUT0              (CLOCKS_BASE+0x00)
#define CLK_GPOUT1              (CLOCKS_BASE+0x0C)
#define CLK_GPOUT2              (CLOCKS_BASE+0x18)
#define CLK_GPOUT3              (CLOCKS_BASE+0x24)
#define CLK_REF	                (CLOCKS_BASE+0x30)
#define CLK_SYS	                (CLOCKS_BASE+0x3C)
#define CLK_PERI                (CLOCKS_BASE+0x48)
#define CLK_USB	                (CLOCKS_BASE+0x54)
#define CLK_ADC	                (CLOCKS_BASE+0x60)
#define CLK_RTC	                (CLOCKS_BASE+0x6C)
#define CLK_SYS_RESUS_CTRL      (CLOCKS_BASE+0x78)
#define CLK_SYS_RESUS_STATUS    (CLOCKS_BASE+0x7C)

#define	CLK_x_CTRL              (0x00)
#define	CLK_x_DIV               (0x04)
#define	CLK_x_SELECTED          (0x08)

#define CLK_SYS_CTRL_AUXSRC     (0x000000e0)
#define CLK_SYS_CTRL_SRC        (0x00000001)
#define CLK_REF_CTRL_SRC        (0x00000003)
#define CLK_CTRL_ENABLE	        (0x00000800)

#define CLK_SYS_CTRL_SRC_AUX	(0x1)

#define CLK_KIND_GPOUT0         0
#define CLK_KIND_GPOUT1         1
#define CLK_KIND_GPOUT2         2
#define CLK_KIND_GPOUT3         3
#define CLK_KIND_REF            4
#define CLK_KIND_SYS            5
#define CLK_KIND_PERI           6
#define CLK_KIND_USB            7
#define CLK_KIND_ADC            8
#define CLK_KIND_RTC            9

/* Reset Controler */
#define RESETS_BASE             0x4000C000
#define RESETS_RESET            (RESETS_BASE+0x0)
#define RESETS_WDSEL            (RESETS_BASE+0x4)
#define RESETS_RESET_DONE       (RESETS_BASE+0x8)

#define RESETS_RESET_ADC        (0x00000001)
#define RESETS_RESET_I2C0       (0x00000008)
#define RESETS_RESET_I2C1       (0x00000010)
#define RESETS_RESET_IO_BANK0   (1<<5)
#define RESETS_RESET_PADS_BANK0 (1<<8)
#define RESETS_RESET_PWM        (1<<14)
#define RESETS_RESET_SPI0       (1<<16)

/* GPIO */
#define IO_BANK0_BASE           0x40014000
#define	GPIO_CTRL(n)            (IO_BANK0_BASE+0x04+(n*8))
#define IO_BANK0_INTR(n)        (IO_BANK0_BASE+0xF0+((n)*4))
#define IO_BANK0_PROC0_INTE(n)  (IO_BANK0_BASE+0x100+((n)*4))
#define IO_BANK0_PROC0_INTS(n)  (IO_BANK0_BASE+0x120+((n)*4))

#define	GPIO_CTRL_FUNCSEL_I2C   3
#define GPIO_CTRL_FUNCSEL_SIO   5
#define GPIO_CTRL_FUNCSEL_SPI   1
#define GPIO_CTRL_FUNCSEL_PWM   4
#define	GPIO_CTRL_FUNCSEL_NULL  31

#define PADS_BANK0_BASE         0x4001c000
#define	GPIO(n)                 (PADS_BANK0_BASE+0x4+(n*4))

#define	GPIO_OD                 (1<<7)
#define	GPIO_IE                 (1<<6)
#define	GPIO_DRIVE_2MA          (0<<4)
#define	GPIO_DRIVE_4MA          (1<<4)
#define	GPIO_DRIVE_8MA          (2<<4)
#define	GPIO_DRIVE_12MA         (3<<4)
#define	GPIO_PUE                (1<<3)
#define	GPIO_PDE                (1<<2)
#define	GPIO_SHEMITT            (1<<1)
#define	GPIO_SLEWDAST           (1<<0)

/* Crystal Oscillator(XOSC) */
#define XOSC_BASE               0x40024000
#define XOSC_CTRL               (XOSC_BASE+0x00)
#define XOSC_STATUS             (XOSC_BASE+0x04)
#define XOSC_STARTUP            (XOSC_BASE+0x0C)

#define	XOSC_CTRL_ENABLE        (0x00FAB000)
#define	XOSC_CTRL_DISABLE       (0x00D1E000)
#define	XOSC_CTRL_FRANG_1_15MHZ (0x00000AA0)

#define	XOSC_STATUS_STABLE      (0x80000000)

/* PLL */
#define PLL_SYS_BASE            (0x40028000)
#define PLL_USB_BASE            (0x4002C000)

#define PLL_CS                  (0x00)
#define	PLL_PWR                 (0x04)
#define	PLL_FBDIV_INT           (0x08)
#define	PLL_PRIM                (0x0C)

#define	PLL_CS_LOCK             (1<<31)
#define	PLL_PWR_PD              (1<<0)
#define	PLL_PWR_VCOPD           (1<<5)
#define	PLL_PWR_POSTDIVPD       (1<<3)
#define PLL_PRIM_POSTDIV1_LSB   (16)
#define PLL_PRIM_POSTDIV2_LSB   (12)

/* UART */
#define UART0_BASE              0x40034000
#define UART1_BASE              0x40038000

#define UARTx_DR                (0x000)
#define UARTx_RSR_ECR           (0x004)
#define UARTx_FR                (0x018)
#define UARTx_IBRD              (0x024)
#define UARTx_FBRD              (0x028)
#define UARTx_LCR_H             (0x02C)
#define UARTx_CR                (0x030)
#define UARTx_IMSC              (0x038)
#define UARTx_MIS               (0x040)
#define UARTx_ICR               (0x044)

#define UART_CR_RXE             (1<<9)
#define UART_CR_TXE             (1<<8)
#define UART_CR_EN              (1<<0)
#define UART_FR_RXFE            (1<<4)  // Receive FIFO Empty (1: empty, 0: not empty)
#define UART_FR_TXFF            (1<<5)  // Transmit FIFO Full（1: full, 0: not full）
#define UART_RSR_OE             (1<<3)

#define UART_IMSC_RXIM          (1<<4)  // Receive FIFO interrupt mask
#define UART_IMSC_RTIM          (1<<6)  // Receive timeout interrupt mask
#define UART_IMSC_OEIM          (1<<10) // Overrun error interrupt mask

#define UART_MIS_RXMIS          (1<<4)  // Receive FIFO masked interrupt status
#define UART_MIS_RTMIS          (1<<6)  // Receive timeout masked interrupt status
#define UART_MIS_OEMIS          (1<<10) // Overrun error masked interrupt status

#define UART_ICR_RXIC           (1<<4)  // Receive FIFO interrupt clear
#define UART_ICR_RTIC           (1<<6)  // Receive timeout interrupt clear
#define UART_ICR_OEIC           (1<<10) // Overrun error interrupt clear

/* I2C */
#define I2C0_BASE                  0x40044000
#define I2C1_BASE                  0x40048000

#define I2Cx_CON                   0x000 // マスター、通信速度などの動作設定
#define I2Cx_TAR                   0x004 // 通信相手の7ビットアドレス
#define I2Cx_DATA_CMD              0x010 // 送受信データと読み書き命令
#define I2Cx_FS_SCL_HCNT           0x01C // Fast mode時のSCL High期間
#define I2Cx_FS_SCL_LCNT           0x020 // Fast mode時のSCL Low期間
#define I2Cx_INTR_MASK             0x030 // 割込みマスク
#define I2Cx_RAW_INTR_STAT         0x034 // マスク前の割込み状態
#define I2Cx_RX_TL                 0x038 // 受信FIFO割込みのしきい値
#define I2Cx_TX_TL                 0x03C // 送信FIFO割込みのしきい値
#define I2Cx_CLR_INTR              0x040 // 全割込みのクリア（読み出しでクリア）
#define I2Cx_CLR_TX_ABRT           0x054 // 送信中断割込みのクリア（読み出しでクリア）
#define I2Cx_CLR_STOP_DET          0x060 // STOP検出割込みのクリア（読み出しでクリア）
#define I2Cx_ENABLE                0x06C // I2Cコントローラの有効／無効設定
#define I2Cx_STATUS                0x070 // I2Cコントローラの動作状態
#define I2Cx_TXFLR                 0x074 // 送信FIFO内のデータ数
#define I2Cx_RXFLR                 0x078 // 受信FIFO内のデータ数
#define I2Cx_SDA_HOLD              0x07C // SDAのホールド時間
#define I2Cx_TX_ABRT_SOURCE        0x080 // 送信中断の原因
#define I2Cx_ENABLE_STATUS         0x09C // I2Cコントローラの有効状態
#define I2Cx_FS_SPKLEN             0x0A0 // Fast mode時のSCLノイズ除去幅

/* IC_CON */
#define I2C_CON_MASTER_MODE        (1U << 0) // マスターモード
#define I2C_CON_SPEED_FAST         (2U << 1) // Fast mode設定
#define I2C_CON_RESTART_EN         (1U << 5) // RESTART条件を有効化
#define I2C_CON_SLAVE_DISABLE      (1U << 6) // スレーブ機能を無効化
#define I2C_CON_TX_EMPTY_CTRL      (1U << 8) // 送信完了後にTX_EMPTYを通知

/* IC_DATA_CMD */
#define I2C_DATA_CMD_READ          (1U << 8)  // 1:読み出し、0:書き込み
#define I2C_DATA_CMD_STOP          (1U << 9)  // 転送後にSTOP条件を生成
#define I2C_DATA_CMD_RESTART       (1U << 10) // 転送前にRESTART条件を生成

/* IC_RAW_INTR_STAT */
#define I2C_RAW_TX_EMPTY           (1U << 4)  // 送信FIFOが空
#define I2C_RAW_TX_ABRT            (1U << 6)  // 送信中断
#define I2C_RAW_STOP_DET           (1U << 9)  // STOP条件検出

/* IC_ENABLE / IC_ENABLE_STATUS */
#define I2C_ENABLE_EN              (1U << 0) // I2Cコントローラ有効

/* SPI (ARM PrimeCell SSP) */
#define SPI0_BASE                  0x4003C000

#define SPIx_CR0                   0x000
#define SPIx_CR1                   0x004
#define SPIx_DR                    0x008
#define SPIx_SR                    0x00C
#define SPIx_CPSR                  0x010
#define SPIx_IMSC                  0x014
#define SPIx_ICR                   0x020

#define SPI_CR0_DSS_8BIT           0x07U
#define SPI_CR0_SCR(n)             ((UW)(n) << 8)
#define SPI_CR1_SSE                (1U << 1)
#define SPI_SR_TFE                 (1U << 0)
#define SPI_SR_TNF                 (1U << 1)
#define SPI_SR_RNE                 (1U << 2)
#define SPI_SR_BSY                 (1U << 4)

/* PWM */
#define PWM_BASE                    0x40050000
#define PWM_SLICE_STRIDE            0x14U
#define PWM_CSR(slice)              (PWM_BASE + ((slice) * PWM_SLICE_STRIDE) + 0x00U)
#define PWM_DIV(slice)              (PWM_BASE + ((slice) * PWM_SLICE_STRIDE) + 0x04U)
#define PWM_CTR(slice)              (PWM_BASE + ((slice) * PWM_SLICE_STRIDE) + 0x08U)
#define PWM_CC(slice)               (PWM_BASE + ((slice) * PWM_SLICE_STRIDE) + 0x0CU)
#define PWM_TOP(slice)              (PWM_BASE + ((slice) * PWM_SLICE_STRIDE) + 0x10U)
#define PWM_EN                      (PWM_BASE + 0xA0U)
#define PWM_CSR_EN                  (1U << 0)
#define PWM_DIV_INT(value)          ((UW)(value) << 4)
#define PWM_CC_B(level)             ((UW)(level) << 16)

/* IOPORT レジスタ */
#define SIO_BASE                0xD0000000
#define	GPIO_IN                 (SIO_BASE+0x04)
#define GPIO_OUT                (SIO_BASE+0x10)
#define GPIO_OUT_SET            (SIO_BASE+0x14)
#define GPIO_OUT_CLR            (SIO_BASE+0x18)
#define GPIO_OUT_XOR            (SIO_BASE+0x1C)
#define GPIO_OE_SET             (SIO_BASE+0x24)
#define GPIO_OE_CLR             (SIO_BASE+0x28)
#define GPIO_OE_XOR             (SIO_BASE+0x2C)

/* SysTick レジスタ */
#define SYST_CSR                (0xE000E010)
#define SYST_RVR                (0xE000E014)
#define SYST_CVR                (0xE000E018)

#define SYST_CSR_COUNTFLAG      (1<<16)
#define SYST_CSR_CLKSOURCE      (1<<2)
#define SYST_CSR_TICKINT        (1<<1)
#define SYST_CSR_ENABLE	        (1<<0)

/* クロック周波数 */
#define	CLOCK_XOSC              (12000000UL)
#define	CLOCK_REF               (CLOCK_XOSC)
#define	CLOCK_PERI              (CLOCK_SYS)

#define	XOSC_MHz                (12)
#define	XOSC_KHz                (XOSC_MHz*1000)

#define	TMCLK_MHz               (125)
#define	TMCLK_KHz               (TMCLK_MHz*1000)
#define	TIMER_PERIOD            (10)

#define	KHz                     (1000)
#define	MHz                     (KHz*1000)

/* NVIC レジスタ */
#define NVIC_ISER               (0xE000E100)
#define NVIC_ICPR               (0xE000E280)
#define SCB_SHPR3               (0xE000ED20)

#define UART0_IRQ_NUM           20
#define UART0_IRQ_MASK          (1UL << UART0_IRQ_NUM)
#define IO_IRQ_BANK0_NUM        13
#define IO_IRQ_BANK0_MASK       (1UL << IO_IRQ_BANK0_NUM)

#define	INTLEVEL_0              (0x00)
#define	INTLEVEL_1              (0x40)
#define	INTLEVEL_2              (0x80)
#define	INTLEVEL_3              (0xC0)

#endif  /* SYSDEF_H */
