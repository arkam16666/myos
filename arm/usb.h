// usb.h - MyOS as a USB serial port
#ifndef USB_H
#define USB_H

void usb_init(void);
int usb_take_connect_event(void);   // 1 once each time a terminal opens the port

#endif
