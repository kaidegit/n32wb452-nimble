#include "boot_config.h"
#include "bridge_protocol.h"
#include "json.hpp"
#include <cctype>
#include <fstream>
#include <set>
#include <stdexcept>

using nlohmann::json;

static std::vector<uint8_t> parse_array_hex(const std::string &hex)
{
    std::string digits;
    for (unsigned char character : hex) {
        if (std::isspace(character)) {
            continue;
        }
        if (!std::isxdigit(character)) {
            throw std::runtime_error("array hex must contain hex pairs and whitespace");
        }
        digits += character;
    }
    if (digits.empty() || digits.size() % 2 != 0 || digits.size() / 2 > BRIDGE_ARRAY_MAX) {
        throw std::runtime_error("array must contain 1..16384 complete bytes");
    }

    std::vector<uint8_t> data;
    data.reserve(digits.size() / 2);
    for (size_t offset = 0; offset < digits.size(); offset += 2) {
        data.push_back(static_cast<uint8_t>(std::stoul(digits.substr(offset, 2), nullptr, 16)));
    }
    return data;
}

static BootStep parse_step(const json &entry)
{
    const std::string operation = entry.at("op").get<std::string>();
    BootStep step{};
    if (operation == "start") {
        step.operation = bridge_Operation_START;
    } else if (operation == "reset") {
        step.operation = bridge_Operation_RESET;
    } else if (operation == "bypass") {
        step.operation = bridge_Operation_BYPASS;
    } else if (operation == "hci-enable") {
        step.operation = bridge_Operation_HCI_ENABLE;
    } else if (operation == "array") {
        step.operation = bridge_Operation_ARRAY_BEGIN;
        step.name = entry.at("name").get<std::string>();
        if (step.name.empty()) {
            throw std::runtime_error("array name must not be empty");
        }
        const auto &command_json = entry.at("command");
        if (!command_json.is_number_integer()) {
            throw std::runtime_error("array command must be an integer");
        }
        const int64_t command = command_json.get<int64_t>();
        if (command != BRIDGE_COMMAND_RAMCODE && command != BRIDGE_COMMAND_PATCH &&
            command != BRIDGE_COMMAND_NVDS) {
            throw std::runtime_error("array command must be 187 (RAM), 204 (patch), or 221 (NVDS)");
        }
        step.command = static_cast<uint8_t>(command);
        step.data = parse_array_hex(entry.at("hex").get<std::string>());
    } else {
        throw std::runtime_error("unknown boot operation: " + operation);
    }
    return step;
}

std::vector<BootStep> load_boot_config(const char *path)
{
    std::ifstream file(path);
    if (!file) {
        throw std::runtime_error(std::string("cannot read boot config: ") + path);
    }
    json config;
    file >> config;
    if (!config.at("version").is_number_integer() || config.at("version").get<int>() != 1) {
        throw std::runtime_error("unsupported boot config version");
    }
    if (!config.at("steps").is_array()) {
        throw std::runtime_error("steps must be an array");
    }

    bool in_rom = false;
    bool downloaded_array = false;
    std::set<std::string> names;
    std::vector<BootStep> steps;
    for (const auto &entry : config.at("steps")) {
        BootStep step = parse_step(entry);
        switch (step.operation) {
        case bridge_Operation_START:
        case bridge_Operation_RESET:
            in_rom = true;
            downloaded_array = false;
            break;
        case bridge_Operation_ARRAY_BEGIN:
            if (!in_rom) {
                throw std::runtime_error("array requires a preceding start/reset");
            }
            if (!names.insert(step.name).second) {
                throw std::runtime_error("duplicate array name: " + step.name);
            }
            downloaded_array = true;
            break;
        case bridge_Operation_BYPASS:
            if (!in_rom) {
                throw std::runtime_error("bypass requires ROM state");
            }
            break;
        case bridge_Operation_HCI_ENABLE:
            if (!in_rom || !downloaded_array) {
                throw std::runtime_error("hci-enable requires firmware download first");
            }
            in_rom = false;
            break;
        default:
            throw std::runtime_error("invalid boot operation");
        }
        steps.push_back(std::move(step));
    }
    if (steps.empty() || steps.back().operation != bridge_Operation_HCI_ENABLE) {
        throw std::runtime_error("boot config must end with hci-enable");
    }
    return steps;
}
