/* Native tests for the shared wire codec, including the 64-byte HID boundary. */
#include <assert.h>
#include "bridge_protocol.h"

int main(void)
{
    assert(bridge_crc16((const uint8_t *)"123456789", 9) == 0xfee8);
    assert(bridge_crc16(NULL, 0) == 0);

    bridge_Control request = bridge_Control_init_zero;
    request.sequence = UINT32_MAX;
    request.operation = bridge_Operation_ARRAY_DATA;
    request.offset = BRIDGE_ARRAY_MAX - sizeof(request.data.bytes);
    request.data.size = sizeof(request.data.bytes);
    for (size_t i = 0; i < request.data.size; i++) {
        request.data.bytes[i] = (uint8_t)i;
    }
    uint8_t report[BRIDGE_REPORT_SIZE];
    assert(bridge_pack(report, &request) == 0);
    assert(report[1] <= 62);

    bridge_Control decoded;
    assert(bridge_unpack(report, &decoded) == 0);
    assert(decoded.sequence == request.sequence);
    assert(decoded.operation == request.operation);
    assert(decoded.offset == request.offset);
    assert(decoded.data.size == request.data.size);
    assert(memcmp(decoded.data.bytes, request.data.bytes, request.data.size) == 0);

    report[0] = 2;
    assert(bridge_unpack(report, &decoded) != 0);
    report[0] = BRIDGE_REPORT_ID;
    report[1] = 63;
    assert(bridge_unpack(report, &decoded) != 0);
    /* A length-delimited field that claims more bytes than the report contains. */
    report[1] = 2;
    report[2] = 0x32;
    report[3] = 40;
    assert(bridge_unpack(report, &decoded) != 0);

    request = (bridge_Control)bridge_Control_init_zero;
    request.result = bridge_Result_VENDOR_ERROR;
    request.vendor_result = -123;
    request.state = bridge_CoreState_ERROR;
    assert(bridge_pack(report, &request) == 0);
    assert(bridge_unpack(report, &decoded) == 0);
    assert(decoded.result == bridge_Result_VENDOR_ERROR);
    assert(decoded.vendor_result == -123);
    assert(decoded.state == bridge_CoreState_ERROR);
    return 0;
}
