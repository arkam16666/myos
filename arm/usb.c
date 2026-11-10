// usb.c - MyOS appears on the PC as a serial port (USB CDC-ACM) over the
// Pi's USB cable. The USB protocol work is done by the TinyUSB library;
// this file describes the device to the PC and hooks TinyUSB into MyOS.
#include "tusb.h"
#include "broadcom/interrupts.h"
#include "broadcom/vcmailbox.h"
#include "timer.h"
#include "usb.h"

static volatile int connect_event;

uint32_t tusb_time_millis_api(void) {
    return (uint32_t)millis();
}

// the USB controller raised an interrupt: let TinyUSB queue the event
void USB_IRQHandler(void) {
    tusb_int_handler(0, true);
}

void usb_init(void) {
    vcmailbox_set_power_state(VCMAILBOX_DEVICE_USB_HCD, true);   // power on the controller
    tusb_rhport_init_t dev_init = {
        .role = TUSB_ROLE_DEVICE,
        .speed = TUSB_SPEED_FULL,
    };
    tusb_init(0, &dev_init);
    BP_SetPriority(USB_IRQn, 0x00);
    BP_ClearPendingIRQ(USB_IRQn);
    BP_EnableIRQ(USB_IRQn);
    BP_EnableIRQs();
}

// a terminal opened the COM port on the PC (DTR goes from off to on).
// Windows reports the line state twice when opening (DTR, then RTS),
// so only react to the change.
static bool last_dtr;

void tud_cdc_line_state_cb(uint8_t itf, bool dtr, bool rts) {
    (void)itf;
    (void)rts;
    if (dtr && !last_dtr) {
        connect_event = 1;
    }
    last_dtr = dtr;
}

int usb_take_connect_event(void) {
    int e = connect_event;
    connect_event = 0;
    return e;
}

// ---------------------------------------------------------------
//  Descriptors: how MyOS introduces itself to the PC
// ---------------------------------------------------------------
static const tusb_desc_device_t desc_device = {
    .bLength            = sizeof(tusb_desc_device_t),
    .bDescriptorType    = TUSB_DESC_DEVICE,
    .bcdUSB             = 0x0200,
    .bDeviceClass       = TUSB_CLASS_MISC,          // composite with IAD (CDC)
    .bDeviceSubClass    = MISC_SUBCLASS_COMMON,
    .bDeviceProtocol    = MISC_PROTOCOL_IAD,
    .bMaxPacketSize0    = CFG_TUD_ENDPOINT0_SIZE,
    .idVendor           = 0x1209,                   // pid.codes
    .idProduct          = 0x0001,                   // test PID (private use)
    .bcdDevice          = 0x0100,
    .iManufacturer      = 1,
    .iProduct           = 2,
    .iSerialNumber      = 3,
    .bNumConfigurations = 1,
};

const uint8_t *tud_descriptor_device_cb(void) {
    return (const uint8_t *)&desc_device;
}

enum { ITF_NUM_CDC = 0, ITF_NUM_CDC_DATA, ITF_NUM_TOTAL };

#define EPNUM_CDC_NOTIF   0x81
#define EPNUM_CDC_OUT     0x02
#define EPNUM_CDC_IN      0x82
#define CONFIG_TOTAL_LEN  (TUD_CONFIG_DESC_LEN + TUD_CDC_DESC_LEN)

static const uint8_t desc_configuration[] = {
    TUD_CONFIG_DESCRIPTOR(1, ITF_NUM_TOTAL, 0, CONFIG_TOTAL_LEN, 0x00, 100),
    TUD_CDC_DESCRIPTOR(ITF_NUM_CDC, 4, EPNUM_CDC_NOTIF, 8, EPNUM_CDC_OUT, EPNUM_CDC_IN, 64),
};

const uint8_t *tud_descriptor_configuration_cb(uint8_t index) {
    (void)index;
    return desc_configuration;
}

static const char *const string_desc[] = {
    NULL,                   // 0: language (handled below)
    "MyOS",                 // 1: manufacturer
    "MyOS Console",         // 2: product
    "0001",                 // 3: serial number
    "MyOS Serial",          // 4: CDC interface
};

static uint16_t desc_str[32 + 1];

const uint16_t *tud_descriptor_string_cb(uint8_t index, uint16_t langid) {
    (void)langid;
    size_t count;
    if (index == 0) {
        desc_str[1] = 0x0409;                       // English
        count = 1;
    } else {
        if (index >= sizeof(string_desc) / sizeof(string_desc[0])) {
            return NULL;
        }
        const char *s = string_desc[index];
        count = 0;
        while (s[count] && count < 32) {
            desc_str[1 + count] = (uint8_t)s[count];  // ASCII -> UTF-16
            count++;
        }
    }
    desc_str[0] = (uint16_t)((TUSB_DESC_STRING << 8) | (2 * count + 2));
    return desc_str;
}
