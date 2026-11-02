# Bridge HID macOS 板级测试（2026-11-02）

本轮使用本机 macOS 命令行工具实际烧录、运行和测试，时间为 Asia/Shanghai 21:21–21:28。

## 结果

- CDC + HID 复合设备在 macOS 枚举成功，VID:PID=`34b7:0002`，bcdDevice=`0200`。
- 主核复位后 `core-status` 返回 0，且保持小核复位；原 `N32-NimBLE` 广播消失。
- START/RESET、四数组单步上传、两次 bypass、HCI_ENABLE 均在实机通过。
- JSON 完整 boot 多次通过，状态为 2；HCI `info` 返回 `host synced: yes`。
- 默认不带 `--port` 的 `info` 和 `scan` 均通过，scan 收到附近 BLE 广播。
- `tools/macos-ble/ble.sh` 自检与 CoreBluetooth 扫描通过，交叉确认 Mac 蓝牙正常。
- 原生 CTest 三项全部通过。
- 发现并修复串口自动识别问题：原实现会在 CMSIS-DAP 共存时错误选择调试器 CDC。

## 环境与目标确认

| 项目 | 实际值 |
| --- | --- |
| 系统 | macOS 15.7.5 / Darwin 24.6.0，arm64 |
| 调试器 | CherryUSB CMSIS-DAP，`0d28:0204`，唯一探针 |
| 目标 | N32WB452CEQ6；CPUID=`410fc241`，DBGMCU ID=`8fb84511` |
| 原固件 | 读回 Flash 中包含 `n32wb452-nimble`、`mode: nimble` 和 `N32-NimBLE` |
| MCU 固件 | `build-bridge/n32wb452_ble_bridge` |
| MCU 构建 | 53884 B Flash / 98880 B RAM |
| Bridge CDC | `/dev/cu.usbmodem0000000000021` |
| 调试器 CDC | `/dev/cu.usbmodem84102`（确认不会作为 bridge 使用） |
| 主机工具 | `tools/blectl/build/blectl` + 系统 HIDAPI |
| Mac 蓝牙工具 | `tools/macos-ble/ble.sh`，CoreBluetooth |
| 当前最终状态 | 保留新 bridge 固件，完整 boot 后为 HCI 状态 2 |

烧录前读回原 Flash 前 96 KiB，备份为 `/tmp/n32-before-hid.bin`。
测试命令与逐步 stdout/stderr 保存于 `/tmp/n32-bridge-hid-macos-20261102/`，
汇总 `results.json`；以下记录避免展开附近设备标识。

## 关键命令与实测

```sh
cmake -S tools/blectl -B tools/blectl/build
cmake --build tools/blectl/build -j8
ctest --test-dir tools/blectl/build --output-on-failure
tools/blectl/build/blectl check-config

cmake -S . -B build-bridge -DN32WB452_APP_MODE=bridge
cmake --build build-bridge -j8
probe-rs download --chip-description-path N32WB452_Series.yaml \
  --chip N32WB452CEQ6 --speed 1000 build-bridge/n32wb452_ble_bridge
probe-rs reset --chip-description-path N32WB452_Series.yaml --chip N32WB452CEQ6
```

USB 复位后需等待设备重新枚举；立即请求可能得到 `HID SET_REPORT failed`，等待枚举完成后正常。
RTT 实际启动输出：

```text
=== n32wb452-nimble (cdc-bridge) ===
sysclk=144000000 Hz  FreeRTOS heap=57344 bytes
I [main] mode: cdc-bridge
I [hci.spi] transport up (irq_line=0)
I [bridge] CDC + HID bridge started; controller held in reset
```

| 测试 | 命令（省略 `tools/blectl/build/blectl` 前缀） | 实际结果 |
| --- | --- | --- |
| 上电策略 | 主核 reset 后 `core-status` | state=0，未自动启动小核 |
| 状态拒绝 | state=0 时 `bypass` | exit=1，invalid core/upload state，符合预期 |
| 启动 | `core-start` | exit=0，state=1 |
| 首数组 | `--array CopyArray array` | 2642 字节，CRC=7257，state=1 |
| Copy bypass | `bypass` | exit=0，state=1 |
| Ramcode | `--array RamcodeArray array` | 476 字节，CRC=ceed，state=1 |
| Patch | `--array PatchArray array` | 3200 字节，CRC=0277，state=1 |
| NVDS | `--array NvdsArray array` | 186 字节，CRC=67f5，state=1 |
| 最后 bypass | `bypass` | exit=0，state=1 |
| 开启 HCI | `hci-enable` | exit=0，state=2 |
| HCI 同步 | `--no-boot info`（不指定端口） | 正确选择 bridge CDC，host synced=yes |
| 默认完整启动 | `info`（不指定端口、不跳过 boot） | 四数组 CRC 正确，host synced=yes |
| 扫描 | `-t 3 scan`（默认 boot 和自动端口） | exit=0，收到多台附近设备广播 |
| 运行中复位 | `core-reset` | exit=0，state=1 |
| 再次启动 | `boot` | exit=0，四数组重新下载，state=2 |
| 原生回归 | `ctest --test-dir tools/blectl/build --output-on-failure` | 3/3 passed |

`info` 的实际控制器 public 地址输出为 `66:84:68:26:AB:EF`。
NimBLE 会打印 `Failed to restore IRKs from store; status=8`，不影响本轮 HCI 同步和扫描；
本轮未进行配对/绑定。

## CoreBluetooth 交叉观测

```sh
tools/macos-ble/ble.sh --self-test
tools/macos-ble/ble.sh scan --name N32-NimBLE --timeout 8
tools/macos-ble/ble.sh scan --name N32-NimBLE --timeout 5
tools/macos-ble/ble.sh scan --timeout 5
```

- 工具自检 `passed=true`，exit=0。
- 烧录前：8 秒扫描中，`matched_devices=1`、`unique_devices=13`，确认原 N32-NimBLE 正常广播。
- 烧录 bridge、小核保持复位时：5 秒扫描，`matched_devices=0`，仍收到 8 台其他设备。
- bridge boot 后：Mac 的 5 秒无过滤扫描收到 8 台设备，Mac 蓝牙扫描正常。
- 原始 JSONL：`/tmp/bridge-macos-pre-scan.jsonl`、
  `/tmp/bridge-macos-reset-held-scan.jsonl`、`/tmp/bridge-macos-post-boot-scan.jsonl`。

bridge 的 blectl 当前是 BLE central，不主动广播；本轮没有将“未发现 N32 广播”误判为
下载失败，HCI 同步与 bridge 自身扫描作为下载成功证据。

## 发现的串口识别问题与修复

原 `hci_cdc_autodetect_port()` 解析 `ioreg -p IOUSB -w 0` 文本，期望串口节点和 USB
产品属性同时出现。实际该调用既未输出属性，IOUSB 平面也不含 serial child service。
在 bridge 与调试器共存时，两者均得到 `unknown product`，最终错误回退到第一个端口：

```text
blectl: multiple CDC devices found:
  /dev/cu.usbmodem84102  (unknown product)
  /dev/cu.usbmodem0000000000021  (unknown product)
blectl: pick one with --port; using /dev/cu.usbmodem84102
blectl: controller did not sync (is the bridge firmware running?)
```

修复为直接通过 IOKit 枚举 IOSerialBSDClient，再沿 IOService 祖先查 USB VID/PID，
仅接受 `34b7:0002`，多块匹配时要求显式 `--port`。工具链接 IOKit/CoreFoundation；
无额外三方库。显式端口先验证成功后，已复测不带端口的 info、scan，均通过。

## 尚未覆盖

- 物理断电重上电；已验证主核复位后的初始 held-reset 状态。
- Bridge 的连接/GATT/echo：当前没有已确认可连接的测试外设，未连接附近无关设备。
- 多块 N32 bridge 同时连接、长时间压力测试、配对和绑定。
