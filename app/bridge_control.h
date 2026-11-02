#ifndef BRIDGE_CONTROL_H
#define BRIDGE_CONTROL_H
#include "usbd_core.h"
#include "bridge.pb.h"
extern volatile bridge_CoreState bridge_core_state;
struct usbd_interface *bridge_control_init(void);
void bridge_control_usb_reset(void);
#define BRIDGE_HID_REPORT_DESC_SIZE 23
#endif
