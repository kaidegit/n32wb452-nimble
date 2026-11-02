#ifndef BLECTL_BRIDGE_HID_H
#define BLECTL_BRIDGE_HID_H
#ifdef __cplusplus
extern "C" {
#endif
/* command: boot, core-start, core-reset, core-status, bypass, array, check-config */
int bridge_hid_run(const char *command, const char *config, const char *path,
                   const char *array_name);
#ifdef __cplusplus
}
#endif
#endif
