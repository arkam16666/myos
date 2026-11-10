// tusb_config.h - TinyUSB settings for MyOS: one USB serial port (CDC-ACM)
#ifndef TUSB_CONFIG_H
#define TUSB_CONFIG_H

// CFG_TUSB_MCU comes from the build (-DCFG_TUSB_MCU=OPT_MCU_BCM2837)
#define CFG_TUSB_OS                 OPT_OS_NONE
#define CFG_TUSB_DEBUG              0
#define CFG_TUSB_MEM_ALIGN          __attribute__((aligned(4)))

#define CFG_TUD_ENABLED             1
#define CFG_TUD_MAX_SPEED           OPT_MODE_FULL_SPEED
#define CFG_TUD_ENDPOINT0_SIZE      64

// the Pi's USB controller (Synopsys DWC2): CPU copies the data, no DMA
#define CFG_TUD_DWC2_SLAVE_ENABLE   1
#define CFG_TUD_DWC2_DMA_ENABLE     0

#define CFG_TUD_CDC                 1
#define CFG_TUD_MSC                 0
#define CFG_TUD_HID                 0
#define CFG_TUD_MIDI                0
#define CFG_TUD_VENDOR              0

#define CFG_TUD_CDC_RX_BUFSIZE      512
#define CFG_TUD_CDC_TX_BUFSIZE      512
#define CFG_TUD_CDC_EP_BUFSIZE      64

#endif
