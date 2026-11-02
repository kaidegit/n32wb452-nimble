# Shared GATT throughput service

`gatt_perf.c` runs unchanged on the MCU NimBLE host and the blectl desktop
NimBLE host. The Mac CoreBluetooth client in `tools/macos-gatt-perf` acts as
central in both cases. This compares the complete host/transport paths against
the same NZ8801 controller. It does not measure 2M PHY or L2CAP CoC.

The test service is `7A02480E-6D4D-4E8E-B7A2-D1C4A2F13E01`:

| UUID suffix | Operation | Purpose |
|---|---|---|
| `3E05` | read/write | Control and receiver/producer statistics |
| `3E06` | write without response/notify | Saturated upload/download |

All integers are little endian. A control write is exactly 12 bytes:

| Offset | Size | Value |
|---|---|---|
| 0 | 1 | Version 1 |
| 1 | 1 | Transport 1 (GATT) |
| 2 | 1 | Direction 1 (Mac upload), 2 (Mac download) |
| 3 | 1 | Operation 0 stop, 1 start, 2 prepare connection interval, 3 request DLE |
| 4 | 2 | Payload size, 4..244 and no larger than negotiated ATT MTU minus 3 |
| 6 | 2 | Reserved, zero |
| 8 | 4 | Duration in milliseconds, 1..3600000 |

Operation 3 explicitly requests TX DLE of 251 octets / 2120 microseconds.
It rejects requests during a transfer, logs the real HCI return code, and returns
an ATT error when the controller refuses the request. A successful request does
not imply negotiated DLE: only the actual HCI Data Length Change event populates
statistics. Baseline runs omit operation 3; the client `--request-dle` option sends
it, waits one second, then sends operation 2 to avoid concurrent LL procedures.

Operation 2 requests a 7.5..15 ms connection interval. The central chooses the
actual interval. Download requires notifications enabled before start. An
active transfer rejects a second start. Stop and disconnect cancel the producer.
The upload runs until an explicit stop so the Mac can drain queued writes.

Every payload starts with a four-byte sequence number, initially zero. Byte
`i >= 4` is `(sequence * 131 + i * 17) & 255`. The receiver checks length,
sequence, and every pattern byte. Upload bytes count actual received data;
download bytes count notifications accepted by the host. The client must drain
and compare those totals with its own received count before reporting success.

Statistics are a 100-byte version 5 record, extending the existing macOS
version 4 benchmark format:

| Offset | Size | Meaning |
|---|---|---|
| 0..4 | 5 | Version 5, transport 1, direction, running, producer done |
| 5 | 2 | Payload size |
| 7 | 1 | Negotiated ATT MTU (service supports at most 247) |
| 8/12/16 | 4 each | Bytes / packets / validation or producer errors |
| 20 | 4 | Time from start to last data, whole milliseconds |
| 24/28 | 4 each | Allocation retries / current free mbufs |
| 32 | 4 | Last explicit DLE HCI request return code (signed host error) |
| 36 | 1 | Explicit DLE request attempted on this connection |
| 37..79 | 43 | Reserved; controller radio counters unavailable on NZ8801 |
| 80/84 | 4 each | Producer blocked iterations / minimum observed free mbufs |
| 88/89 | 1 each | Effective TX/RX DLE octets observed from HCI; zero means unknown |
| 90/92 | 2 each | Observed DLE TX/RX time in microseconds; zero means unknown |
| 94 | 2 | Actual connection interval in units of 1.25 ms |
| 96 | 4 | Exact time from start to last data, microseconds |

Version 5 uses offset 96 for precise elapsed microseconds instead of the
controller-specific LL queue depth in version 4. The MCU provides a hardware
1 MHz timestamp; the desktop host uses `CLOCK_MONOTONIC`. Unsigned subtraction
handles a 32-bit microsecond wrap; the maximum run time is below that wrap period.
The producer callout still uses the NPL scheduler, independently of measurement.
Reserved bytes are zero. No PHY or DLE capability is inferred from ATT MTU.
The closed controller rejects PHY HCI commands: the diagnostic characteristic
retains the real HCI status while reporting the mandatory LE default 1M PHY.

The new NimBLE GATT schema uses public address `1E:AB:26:68:84:67` instead of the
old `...:66` identity to avoid CoreBluetooth retaining the old characteristic
cache. `blectl perf-server` advertises `N32-Bridge-Perf` with default address
`1E:AB:26:68:84:68`; an explicit `--bdaddr` overrides it. These are development
identities for this single-board test, not globally unique production addresses.

Run the desktop server with the bridge firmware attached:

```sh
tools/blectl/build/blectl perf-server
```

The default runtime JSON boot occurs first. Use `--no-boot` to reuse a controller
already downloaded over HID. Stop with Ctrl-C. The server supports one connection
and reports advertising/initialization failures through a nonzero exit status.
