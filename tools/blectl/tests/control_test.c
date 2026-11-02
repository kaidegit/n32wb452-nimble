/* Test state transitions and SPI ownership without running USB or an RTOS. */
#define APP_MODE_BRIDGE 1
#include "../../../app/bridge_control.c"

static int s_reset_calls;
static int s_array_calls;
static int s_vendor_failure;
static int s_rx_enabled;
void bridge_spi_lock(void) { }
void bridge_spi_unlock(void) { }
void hci_spi_set_enabled(int enabled) { s_rx_enabled = enabled; }
void eif_reset_ble_core(void) { s_reset_calls++; }
int32_t send_vendor_bypass_command(void) { return s_vendor_failure; }
int32_t send_vendor_array(uint8_t command, const uint8_t *data, uint16_t size, uint16_t crc)
{
    assert(command == BRIDGE_COMMAND_PATCH);
    assert(size == 3 && memcmp(data, "abc", 3) == 0);
    assert(crc == bridge_crc16(data, size));
    s_array_calls++;
    return s_vendor_failure;
}

static bridge_Result operation(bridge_Operation operation)
{
    bridge_Control request = bridge_Control_init_zero;
    request.operation = operation;
    return execute(&request);
}

static void upload(uint16_t crc)
{
    bridge_Control request = bridge_Control_init_zero;
    request.operation = bridge_Operation_ARRAY_BEGIN;
    request.command = BRIDGE_COMMAND_PATCH;
    request.argument = 3;
    request.crc = crc;
    assert(execute(&request) == bridge_Result_OK);
    assert(operation(bridge_Operation_BYPASS) == bridge_Result_INVALID_STATE);
    assert(operation(bridge_Operation_ARRAY_COMMIT) == bridge_Result_INVALID_STATE);

    request.operation = bridge_Operation_ARRAY_DATA;
    request.data.size = 3;
    memcpy(request.data.bytes, "abc", 3);
    request.offset = 1;
    assert(execute(&request) == bridge_Result_INVALID_ARGUMENT);
    request.offset = 0;
    assert(execute(&request) == bridge_Result_OK);
    assert(execute(&request) == bridge_Result_INVALID_ARGUMENT); /* no duplicate append */
}

int main(void)
{
    const uint16_t crc = bridge_crc16((const uint8_t *)"abc", 3);
    assert(bridge_core_state == bridge_CoreState_RESET_HELD);
    assert(operation(bridge_Operation_BYPASS) == bridge_Result_INVALID_STATE);
    assert(operation(bridge_Operation_START) == bridge_Result_OK);
    assert(s_reset_calls == 1 && !s_rx_enabled);

    upload(crc ^ 1);
    assert(operation(bridge_Operation_ARRAY_COMMIT) == bridge_Result_CRC_MISMATCH);
    assert(s_array_calls == 0);
    upload(crc);
    assert(operation(bridge_Operation_ARRAY_COMMIT) == bridge_Result_OK);
    assert(s_array_calls == 1);
    assert(operation(bridge_Operation_ARRAY_COMMIT) == bridge_Result_INVALID_STATE);
    assert(operation(bridge_Operation_BYPASS) == bridge_Result_OK);
    assert(operation(bridge_Operation_HCI_ENABLE) == bridge_Result_OK);
    assert(s_rx_enabled && bridge_core_state == bridge_CoreState_HCI);
    assert(operation(bridge_Operation_BYPASS) == bridge_Result_INVALID_STATE);

    assert(operation(bridge_Operation_RESET) == bridge_Result_OK);
    assert(!s_rx_enabled && bridge_core_state == bridge_CoreState_ROM);
    s_vendor_failure = -12;
    assert(operation(bridge_Operation_BYPASS) == bridge_Result_VENDOR_ERROR);
    assert(bridge_core_state == bridge_CoreState_ERROR);
    assert(operation(bridge_Operation_HCI_ENABLE) == bridge_Result_INVALID_STATE);
    assert(operation(bridge_Operation_RESET) == bridge_Result_OK);
    assert(bridge_core_state == bridge_CoreState_ROM);
    return 0;
}
