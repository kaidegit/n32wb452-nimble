#ifndef BLECTL_BOOT_CONFIG_H
#define BLECTL_BOOT_CONFIG_H
#include <cstdint>
#include <string>
#include <vector>
#include "bridge.pb.h"

struct BootStep {
    bridge_Operation operation;
    std::string name;
    uint8_t command = 0;
    std::vector<uint8_t> data;
};

/* Validate the complete boot sequence before touching the controller. */
std::vector<BootStep> load_boot_config(const char *path);
#endif
