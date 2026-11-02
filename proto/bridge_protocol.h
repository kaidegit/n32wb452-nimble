#ifndef BRIDGE_PROTOCOL_H
#define BRIDGE_PROTOCOL_H
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include "bridge.pb.h"
#include "pb_encode.h"
#include "pb_decode.h"

#define BRIDGE_VID 0x34b7
#define BRIDGE_PID 0x0002
#define BRIDGE_USAGE_PAGE 0xff00
#define BRIDGE_REPORT_ID 1
#define BRIDGE_REPORT_SIZE 64
#define BRIDGE_ARRAY_MAX 16384
#define BRIDGE_COMMAND_RAMCODE 0xbb
#define BRIDGE_COMMAND_PATCH 0xcc
#define BRIDGE_COMMAND_NVDS 0xdd

/* CRC covers every array byte, excluding HID framing and protobuf bytes.
 * poly=0x8005, init=0, MSB first, no reflection, no xorout. */
static inline uint16_t bridge_crc16(const uint8_t *data, size_t len)
{
    uint16_t crc = 0;
    while (len-- > 0) {
        crc ^= (uint16_t)*data++ << 8;
        for (unsigned bit = 0; bit < 8; bit++) {
            const uint16_t polynomial = (crc & 0x8000) ? 0x8005 : 0;
            crc = (uint16_t)((crc << 1) ^ polynomial);
        }
    }
    return crc;
}

/* [report ID][protobuf length][protobuf payload][zero padding]. */
static inline int bridge_pack(uint8_t report[BRIDGE_REPORT_SIZE], const bridge_Control *message)
{
    memset(report, 0, BRIDGE_REPORT_SIZE);
    report[0] = BRIDGE_REPORT_ID;
    pb_ostream_t stream = pb_ostream_from_buffer(report + 2, BRIDGE_REPORT_SIZE - 2);
    if (!pb_encode(&stream, bridge_Control_fields, message)) {
        return -1;
    }
    report[1] = (uint8_t)stream.bytes_written;
    return 0;
}

static inline int bridge_unpack(const uint8_t report[BRIDGE_REPORT_SIZE], bridge_Control *message)
{
    if (report[0] != BRIDGE_REPORT_ID || report[1] > BRIDGE_REPORT_SIZE - 2) {
        return -1;
    }
    memset(message, 0, sizeof(*message));
    pb_istream_t stream = pb_istream_from_buffer(report + 2, report[1]);
    return pb_decode(&stream, bridge_Control_fields, message) ? 0 : -1;
}
#endif
