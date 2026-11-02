#include "bridge_hid.h"
#include "bridge_protocol.h"
#include "boot_config.h"
#include <hidapi.h>
#include <algorithm>
#include <chrono>
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <thread>

static constexpr auto kCommandTimeout = std::chrono::seconds(30);
static constexpr auto kPollInterval = std::chrono::milliseconds(2);

static std::string find_bridge_path()
{
    hid_device_info *devices = hid_enumerate(BRIDGE_VID, BRIDGE_PID);
    std::string selected;
    unsigned matches = 0;
    for (auto *device = devices; device; device = device->next) {
        if (device->usage_page == BRIDGE_USAGE_PAGE && device->usage == 1) {
            selected = device->path;
            matches++;
        }
    }
    hid_free_enumeration(devices);
    if (matches != 1) {
        throw std::runtime_error("expected one bridge HID; use --hid-path to select a device");
    }
    return selected;
}

static void print_array_crc(const BootStep &step, std::ostream &output)
{
    const auto crc = bridge_crc16(step.data.data(), step.data.size());
    output << step.name << ": " << step.data.size() << " bytes, CRC=0x"
           << std::hex << std::setw(4) << std::setfill('0') << crc
           << std::dec << std::setfill(' ') << '\n';
}

static const char *result_description(bridge_Result result)
{
    switch (result) {
    case bridge_Result_OK: return "ok";
    case bridge_Result_BUSY: return "busy";
    case bridge_Result_INVALID_ARGUMENT: return "invalid argument";
    case bridge_Result_INVALID_STATE: return "invalid core/upload state";
    case bridge_Result_CRC_MISMATCH: return "array CRC mismatch";
    case bridge_Result_VENDOR_ERROR: return "vendor SPI command failed; reset required";
    default: return "unknown protocol result";
    }
}

class BridgeConnection {
public:
    explicit BridgeConnection(const char *path)
    {
        if (hid_init() != 0) {
            throw std::runtime_error("hid_init failed");
        }
        try {
            const std::string selected = path ? path : find_bridge_path();
            device_ = hid_open_path(selected.c_str());
            if (!device_) {
                throw std::runtime_error("cannot open bridge HID");
            }
        } catch (...) {
            hid_exit();
            throw;
        }
    }

    ~BridgeConnection()
    {
        hid_close(device_);
        hid_exit();
    }
    BridgeConnection(const BridgeConnection &) = delete;
    BridgeConnection &operator=(const BridgeConnection &) = delete;

    bridge_Control transact(bridge_Control request)
    {
        request.sequence = ++sequence_;
        uint8_t report[BRIDGE_REPORT_SIZE];
        if (bridge_pack(report, &request) != 0) {
            throw std::runtime_error("protobuf report overflow");
        }
        if (hid_send_feature_report(device_, report, sizeof(report)) != sizeof(report)) {
            throw std::runtime_error("HID SET_REPORT failed");
        }

        /* SET_REPORT acknowledges USB delivery, not completion of the SPI command. */
        const auto deadline = std::chrono::steady_clock::now() + kCommandTimeout;
        while (std::chrono::steady_clock::now() < deadline) {
            std::this_thread::sleep_for(kPollInterval);
            report[0] = BRIDGE_REPORT_ID;
            const int received = hid_get_feature_report(device_, report, sizeof(report));
            bridge_Control reply;
            if (received != sizeof(report) || bridge_unpack(report, &reply) != 0) {
                throw std::runtime_error("HID GET_REPORT failed");
            }
            if (reply.sequence != sequence_ || reply.result == bridge_Result_BUSY) {
                continue;
            }
            if (reply.operation != request.operation) {
                throw std::runtime_error("HID reply operation mismatch");
            }
            if (reply.result != bridge_Result_OK) {
                throw std::runtime_error(std::string(result_description(reply.result)) +
                                         " (vendor result " + std::to_string(reply.vendor_result) + ")");
            }
            return reply;
        }
        throw std::runtime_error("HID command timed out; reset controller before retrying");
    }

    bridge_Control execute(bridge_Operation operation)
    {
        bridge_Control request = bridge_Control_init_zero;
        request.operation = operation;
        return transact(request);
    }

    void send_array(const BootStep &step)
    {
        print_array_crc(step, std::cerr);
        bridge_Control request = bridge_Control_init_zero;
        request.operation = bridge_Operation_ARRAY_BEGIN;
        request.argument = step.data.size();
        request.command = step.command;
        request.crc = bridge_crc16(step.data.data(), step.data.size());
        transact(request);

        size_t offset = 0;
        while (offset < step.data.size()) {
            bridge_Control chunk = bridge_Control_init_zero;
            chunk.operation = bridge_Operation_ARRAY_DATA;
            chunk.offset = offset;
            chunk.data.size = std::min(sizeof(chunk.data.bytes), step.data.size() - offset);
            memcpy(chunk.data.bytes, step.data.data() + offset, chunk.data.size);
            transact(chunk);
            offset += chunk.data.size;
        }
        execute(bridge_Operation_ARRAY_COMMIT);
    }

private:
    hid_device *device_ = nullptr;
    uint32_t sequence_ = 0;
};

static bridge_Operation command_operation(const std::string &command)
{
    if (command == "core-start") return bridge_Operation_START;
    if (command == "core-reset") return bridge_Operation_RESET;
    if (command == "core-status") return bridge_Operation_STATUS;
    if (command == "bypass") return bridge_Operation_BYPASS;
    if (command == "hci-enable") return bridge_Operation_HCI_ENABLE;
    throw std::runtime_error("unknown HID command: " + command);
}

extern "C" int bridge_hid_run(const char *command, const char *config,
                               const char *path, const char *array_name)
{
    try {
        const std::string action(command);
        std::vector<BootStep> steps;
        if (action == "boot" || action == "array" || action == "check-config") {
            steps = load_boot_config(config);
        }
        if (action == "check-config") {
            for (const auto &step : steps) {
                if (step.operation == bridge_Operation_ARRAY_BEGIN) {
                    print_array_crc(step, std::cout);
                }
            }
            return 0;
        }

        const BootStep *selected_array = nullptr;
        if (action == "array") {
            for (const auto &step : steps) {
                if (step.operation == bridge_Operation_ARRAY_BEGIN && array_name && step.name == array_name) {
                    selected_array = &step;
                }
            }
            if (!selected_array) {
                throw std::runtime_error("select a configured array with --array NAME");
            }
        }

        BridgeConnection connection(path);
        if (action == "boot") {
            for (const auto &step : steps) {
                if (step.operation == bridge_Operation_ARRAY_BEGIN) {
                    connection.send_array(step);
                } else {
                    connection.execute(step.operation);
                }
            }
        } else if (action == "array") {
            connection.send_array(*selected_array);
        } else {
            connection.execute(command_operation(action));
        }

        const auto status = connection.execute(bridge_Operation_STATUS);
        std::cout << "core state: " << status.state << " (0=reset held, 1=ROM, 2=HCI, 3=error)\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << "blectl: " << error.what() << '\n';
        return 1;
    }
}
