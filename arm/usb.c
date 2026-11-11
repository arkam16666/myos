// usb.c - MyOS appears on the PC as a USB device with two functions:
//   interfaces 0-1: serial port (CDC-ACM)  -> COM port: shell, update, push
//   interfaces 2-3: network adapter (CDC-NCM) -> the Pi at 192.168.7.1 (net.c)
// The USB protocol work is done by the TinyUSB library; this file
// describes the device to the PC and hooks TinyUSB into MyOS.
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
    .bcdUSB             = 0x0210,                   // 2.1: has a BOS descriptor (for Windows)
    .bDeviceClass       = TUSB_CLASS_MISC,          // composite with IADs
    .bDeviceSubClass    = MISC_SUBCLASS_COMMON,
    .bDeviceProtocol    = MISC_PROTOCOL_IAD,
    .bMaxPacketSize0    = CFG_TUD_ENDPOINT0_SIZE,
    .idVendor           = 0x1209,                   // pid.codes
    .idProduct          = 0x0001,                   // test PID (private use)
    .bcdDevice          = 0x0200,                   // changed when the layout changed, so
                                                    // Windows reads the descriptors again
    .iManufacturer      = 1,
    .iProduct           = 2,
    .iSerialNumber      = 3,
    .bNumConfigurations = 1,
};

const uint8_t *tud_descriptor_device_cb(void) {
    return (const uint8_t *)&desc_device;
}

enum { ITF_NUM_CDC = 0, ITF_NUM_CDC_DATA, ITF_NUM_NET, ITF_NUM_NET_DATA, ITF_NUM_TOTAL };
enum { STRID_LANGID = 0, STRID_MANUFACTURER, STRID_PRODUCT, STRID_SERIAL,
       STRID_CDC, STRID_NET, STRID_MAC };

#define EPNUM_CDC_NOTIF   0x81
#define EPNUM_CDC_OUT     0x02
#define EPNUM_CDC_IN      0x82
#define EPNUM_NET_NOTIF   0x83
#define EPNUM_NET_OUT     0x04
#define EPNUM_NET_IN      0x84
#define CONFIG_TOTAL_LEN  (TUD_CONFIG_DESC_LEN + TUD_CDC_DESC_LEN + TUD_CDC_NCM_DESC_LEN)

static const uint8_t desc_configuration[] = {
    TUD_CONFIG_DESCRIPTOR(1, ITF_NUM_TOTAL, 0, CONFIG_TOTAL_LEN, 0x00, 100),
    TUD_CDC_DESCRIPTOR(ITF_NUM_CDC, STRID_CDC, EPNUM_CDC_NOTIF, 8, EPNUM_CDC_OUT, EPNUM_CDC_IN, 64),
    TUD_CDC_NCM_DESCRIPTOR(ITF_NUM_NET, STRID_NET, STRID_MAC, EPNUM_NET_NOTIF, 64,
                           EPNUM_NET_OUT, EPNUM_NET_IN, 64, CFG_TUD_NET_MTU, 50,
                           (uint8_t)(NCM_NETWORK_CAPS_ETH_FILTER | NCM_NETWORK_CAPS_NTB_INPUT_SIZE)),
};

const uint8_t *tud_descriptor_configuration_cb(uint8_t index) {
    (void)index;
    return desc_configuration;
}

// --- Windows: load its built-in NCM driver for interface 2 automatically ---
#define BOS_TOTAL_LEN     (TUD_BOS_DESC_LEN + TUD_BOS_MICROSOFT_OS_DESC_LEN)
#define MS_OS_20_DESC_LEN 0xB2
#define VENDOR_REQUEST_MICROSOFT 1

static const uint8_t desc_bos[] = {
    TUD_BOS_DESCRIPTOR(BOS_TOTAL_LEN, 1),
    TUD_BOS_MS_OS_20_DESCRIPTOR(MS_OS_20_DESC_LEN, VENDOR_REQUEST_MICROSOFT),
};

const uint8_t *tud_descriptor_bos_cb(void) {
    return desc_bos;
}

// Microsoft OS 2.0 descriptor set: the function starting at interface 2 is
// compatible with "WINNCM" (Windows' NCM driver). Adapted from TinyUSB's
// net_lwip_webserver example: Copyright (c) 2019 Ha Thach (tinyusb.org),
// MIT License - see THIRD-PARTY-NOTICES.md.
static const uint8_t desc_ms_os_20[] = {
    // set header: length, type, Windows version (8.1+), total length
    U16_TO_U8S_LE(0x000A), U16_TO_U8S_LE(MS_OS_20_SET_HEADER_DESCRIPTOR), U32_TO_U8S_LE(0x06030000),
    U16_TO_U8S_LE(MS_OS_20_DESC_LEN),
    // configuration subset header
    U16_TO_U8S_LE(0x0008), U16_TO_U8S_LE(MS_OS_20_SUBSET_HEADER_CONFIGURATION), 0, 0,
    U16_TO_U8S_LE(MS_OS_20_DESC_LEN - 0x0A),
    // function subset header: first interface = the network adapter
    U16_TO_U8S_LE(0x0008), U16_TO_U8S_LE(MS_OS_20_SUBSET_HEADER_FUNCTION), ITF_NUM_NET, 0,
    U16_TO_U8S_LE(MS_OS_20_DESC_LEN - 0x0A - 0x08),
    // compatible ID "WINNCM"
    U16_TO_U8S_LE(0x0014), U16_TO_U8S_LE(MS_OS_20_FEATURE_COMPATBLE_ID),
    'W', 'I', 'N', 'N', 'C', 'M', 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    // registry property: DeviceInterfaceGUIDs
    U16_TO_U8S_LE(MS_OS_20_DESC_LEN - 0x0A - 0x08 - 0x08 - 0x14), U16_TO_U8S_LE(MS_OS_20_FEATURE_REG_PROPERTY),
    U16_TO_U8S_LE(0x0007), U16_TO_U8S_LE(0x002A),
    'D', 0x00, 'e', 0x00, 'v', 0x00, 'i', 0x00, 'c', 0x00, 'e', 0x00, 'I', 0x00, 'n', 0x00, 't', 0x00, 'e', 0x00,
    'r', 0x00, 'f', 0x00, 'a', 0x00, 'c', 0x00, 'e', 0x00, 'G', 0x00, 'U', 0x00, 'I', 0x00, 'D', 0x00, 's', 0x00,
    0x00, 0x00,
    U16_TO_U8S_LE(0x0050),
    '{', 0x00, '1', 0x00, '2', 0x00, '3', 0x00, '4', 0x00, '5', 0x00, '6', 0x00, '7', 0x00, '8', 0x00, '-', 0x00,
    '0', 0x00, 'D', 0x00, '0', 0x00, '8', 0x00, '-', 0x00, '4', 0x00, '3', 0x00, 'F', 0x00, 'D', 0x00, '-', 0x00,
    '8', 0x00, 'B', 0x00, '3', 0x00, 'E', 0x00, '-', 0x00, '1', 0x00, '2', 0x00, '7', 0x00, 'C', 0x00, 'A', 0x00,
    '8', 0x00, 'A', 0x00, 'F', 0x00, 'F', 0x00, 'F', 0x00, '9', 0x00, 'D', 0x00, '}', 0x00, 0x00, 0x00, 0x00, 0x00,
};
TU_VERIFY_STATIC(sizeof(desc_ms_os_20) == MS_OS_20_DESC_LEN, "MS OS 2.0 descriptor size");

bool tud_vendor_control_xfer_cb(uint8_t rhport, uint8_t stage, const tusb_control_request_t *request) {
    if (stage != CONTROL_STAGE_SETUP) return true;
    if (request->bmRequestType_bit.type == TUSB_REQ_TYPE_VENDOR &&
        request->bRequest == VENDOR_REQUEST_MICROSOFT && request->wIndex == 7) {
        return tud_control_xfer(rhport, request, (void *)(uintptr_t)desc_ms_os_20, MS_OS_20_DESC_LEN);
    }
    return false;
}

// --- strings ---
static const char *const string_desc[] = {
    NULL,                   // language (handled below)
    "MyOS",                 // manufacturer
    "MyOS Console",         // product
    "0001",                 // serial number
    "MyOS Serial",          // CDC interface
    "MyOS Network",         // NCM interface
    NULL,                   // MAC address (made from tud_network_mac_address)
};

static uint16_t desc_str[32 + 1];

const uint16_t *tud_descriptor_string_cb(uint8_t index, uint16_t langid) {
    (void)langid;
    size_t count = 0;
    if (index == STRID_LANGID) {
        desc_str[1] = 0x0409;                       // English
        count = 1;
    } else if (index == STRID_MAC) {
        for (int i = 0; i < 6; i++) {               // e.g. "0202846A9600"
            desc_str[1 + count++] = "0123456789ABCDEF"[tud_network_mac_address[i] >> 4];
            desc_str[1 + count++] = "0123456789ABCDEF"[tud_network_mac_address[i] & 0xF];
        }
    } else {
        if (index >= sizeof(string_desc) / sizeof(string_desc[0])) return NULL;
        const char *s = string_desc[index];
        while (s[count] && count < 32) {
            desc_str[1 + count] = (uint8_t)s[count];  // ASCII -> UTF-16
            count++;
        }
    }
    desc_str[0] = (uint16_t)((TUSB_DESC_STRING << 8) | (2 * count + 2));
    return desc_str;
}
