# macOS GATT performance client

Independent CoreBluetooth client for this repository. No package dependencies;
`ble.sh` compiles Swift and signs the binary with the stable identifier
`local.n32.macos-gatt-perf`. macOS Bluetooth permission must be enabled.

Flash the chosen firmware first. For bridge mode, also start `blectl perf-server`.
Then run the same Mac central and payload against either server:

```sh
tools/macos-gatt-perf/ble.sh --self-test
python3 tools/macos-gatt-perf/run.py --mode nimble --name N32-NimBLE --request-dle --size 182
python3 tools/macos-gatt-perf/run.py --mode bridge --name N32-Bridge-Perf --request-dle --size 182
```

The runner measures upload (Mac to server, write without response) and download
(server to Mac, notifications), three 30-second repetitions each. It saves raw
JSONL, stderr and report.json under `.build/`, stops on the first failure, and
never flashes, resets or accesses the debug probe while measuring.
Use `--request-dle` to request 251 octets / 2120 us before the interval setup;
the runner requires the observed TX/RX lengths to confirm success. This Mac
negotiates ATT MTU 185, hence the measured payload is 182 bytes. The default
244-byte reference payload is retained and fails explicitly when unsupported.

Each payload contains a sequence number and deterministic pattern. Upload ends
only when server verified bytes equal every Mac queued byte; download ends only
when every successfully queued server byte has been verified by the Mac. Tail
drain is included. Upload uses the server START-to-last-receive clock; download
uses Mac START write completion (or the first notification if earlier) through
the final verified notification. `payload_kbps` is receiver-verified application bytes times
8 / elapsed seconds / 1000. It includes the 4-byte sequence field and excludes
ATT/L2CAP/link headers. Failed runs do not establish throughput.

The runner uses `--legacy-controller`: NZ8801 has no LE PHY read/set command, so
only its known Unknown Command status with default 1M is accepted. This records
`legacy_phy_fallback`, rather than claiming PHY command verification. Effective
DLE values and connection interval are reported by the server, without requiring
DLE251. Sequence, pattern, errors and final byte counts are still checked. Without
this option the client requires successful PHY commands and DLE251.

`ble.sh probe --name N32-NimBLE --echo-perf --legacy-controller --duration 10`
retains the serialized 64-byte write/read echo baseline; its number is round-trip
behavior and cannot be compared directly to saturated one-way throughput.

Reference radio acceptance is reported separately from data integrity. The
CH32 reference requires successful PHY confirmation, observed DLE251 and 244-byte
payloads. Unknown DLE/default 1M fallback runs may establish data integrity and
measured throughput under those limits, but cannot claim that reference PASS.
Use `--strict-radio` on the runner to require the radio checks.

The runner also requires upload/producer backpressure to establish sustained
load. A failed case, malformed output or timeout stops the run without retry
and preserves the failing case and reason. Acceptance checks can be tested
offline with `python3 tools/macos-gatt-perf/test_runner.py`.
