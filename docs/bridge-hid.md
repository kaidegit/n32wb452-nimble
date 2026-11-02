# Bridge HID 控制与运行时固件数组

bridge 枚举为 CDC ACM + vendor HID 复合设备（VID `34b7`，PID `0002`）。CDC
保留 H4 数据通道；HID 专门控制小核，usage page `0xff00`、usage `1`。
bridge 上电仅初始化 GPIO、定时器和 SPI，将小核保持在复位状态，不执行
`ble_hardware_init()` / `ble_initdata_down()`。nimble 固件继续使用原有自动启动流程。

## 构建和使用

blectl 需要 C/C++17 编译器和 HIDAPI。macOS 可通过 `brew install hidapi` 安装 HIDAPI。
工具专用 JSON 库放在 `tools/blectl/third_party/json`；固件和工具共用的 nanopb
放在项目 `third_party/nanopb`，共享协议放在 `proto`。

```sh
cmake -S . -B build-bridge -DN32WB452_APP_MODE=bridge
cmake --build build-bridge
cmake -S tools/blectl -B tools/blectl/build
cmake --build tools/blectl/build
ctest --test-dir tools/blectl/build --output-on-failure

# 无需接板，检查 JSON 并打印重新计算的 CRC
tools/blectl/build/blectl --config tools/blectl/bridge_boot.json check-config

# 执行完整启动流程；不启动 PC NimBLE host
tools/blectl/build/blectl --config tools/blectl/bridge_boot.json boot

# info/scan/probe 默认先读取 JSON 并执行完整启动流程
tools/blectl/build/blectl --config tools/blectl/bridge_boot.json -t 10 scan

# 使用已启动的小核，跳过下载
tools/blectl/build/blectl --no-boot info
```

不传 `--config` 时，读取构建目录中的 `bridge_boot.json`。CMake 初次配置时会复制默认
JSON 到该目录；部署时可将自定义文件放到任意位置并显式传 `--config`，更换数组不需要
重新编译工具或固件。配置文件必须先完整通过校验，blectl 才会打开 HID 并执行操作。

单步控制不自动下载或启动 PC NimBLE：

```sh
tools/blectl/build/blectl core-status
tools/blectl/build/blectl core-start
tools/blectl/build/blectl --array CopyArray array
tools/blectl/build/blectl bypass
# 依次发送 RamcodeArray、PatchArray、NvdsArray 后，再 bypass 和 hci-enable
tools/blectl/build/blectl hci-enable
tools/blectl/build/blectl core-reset
```

`core-start` 和 `core-reset` 都会脉冲复位并进入 ROM 下载状态，清除未完成的上传。
它们不会自动发送任何数组。`bypass` 对应 SDK `send_vendor_bypass_command()`；
`array` 从所选 JSON 中按 `--array NAME` 查找数组，完成上传后调用 SDK
`send_vendor_array()`。上传完成前不允许 bypass 或开启 HCI。

多块 bridge 同时连接时必须用 `--hid-path` 指定 HID 路径，避免自动选择错误设备；
使用 CDC 命令时同时以 `--port` 指定同一块板的串口。单块板会自动发现 HID。
选项统一写在命令之前。仅允许一个 blectl 进程控制同一设备；复位前结束正在运行的扫描或连接进程。

## JSON 格式（版本 1）

`steps` 是有序步骤，顺序就是执行顺序。以下只展示格式，示例中的短数组不能启动小核。
默认可用数组见 `tools/blectl/bridge_boot.json`。

```json
{
  "version": 1,
  "steps": [
    {"op": "start"},
    {"op": "array", "name": "CopyArray", "command": 187, "hex": "00 11 22 33"},
    {"op": "bypass"},
    {"op": "array", "name": "RamcodeArray", "command": 187, "hex": "00 11"},
    {"op": "array", "name": "PatchArray", "command": 204, "hex": "00 22"},
    {"op": "array", "name": "NvdsArray", "command": 221, "hex": "00 33"},
    {"op": "bypass"},
    {"op": "hci-enable"}
  ]
}
```

- `op`：`start` / `reset` / `array` / `bypass` / `hci-enable`。
- `name`：数组的唯一名字，仅供主机选择和日志使用。
- `command`：十进制整数，187=`0xbb` RAM/Copy，204=`0xcc` Patch，221=`0xdd` NVDS。
- `hex`：完整数组内容，十六进制字节对，可含空白，不接受 `0x` 前缀或逗号。
- 每组数组允许 1..16384 字节，固件只缓存一组，避免占用过多 MCU SRAM。
- CRC 不存入 JSON；修改任何字节后，blectl 每次读取都会重新计算。

默认顺序与 SDK `ble_initdata_down()` 一致。可运行
`python3 tools/blectl/export_boot_config.py` 从 SDK `Ramcode.h` 重新导出默认文件。
导出脚本会检查数组长度，并把计算的 CRC 与 SDK 常量比较；运行时更换数组直接编辑 JSON 即可。

CRC 参数：poly=`0x8005`，init=`0x0000`，MSB-first，无输入/输出反转，无 xorout，
覆盖数组全部字节。默认值：Copy=`7257`、Ramcode=`ceed`、Patch=`0277`、Nvds=`67f5`；
`123456789` 的校验值为 `fee8`。固件接收完整数组后再计算 CRC，匹配才发给小核。

## HID / protobuf 协议

定义见 `proto/bridge.proto`，两端共用 nanopb 编解码和固定长度字节数组，无动态解码分配。
HID 使用 EP0 Feature Report，report ID 为 1，总长 64 字节（包含 ID）：

| 字节 | 内容 |
| --- | --- |
| 0 | Report ID = 1 |
| 1 | protobuf 有效长度，0..62 |
| 2.. | `bridge.Control` protobuf 编码，余下补零 |

HID interrupt IN endpoint `0x84` 为接口枚举预留，控制请求及结果均经 EP0 交换。
主机发送 `SET_REPORT(Feature, ID=1)`，再轮询 `GET_REPORT(Feature, ID=1)`。
固件 USB ISR 仅复制请求并通知工作任务；所有小核复位、SPI 下载和应答等待均在任务中执行。
最多一个请求执行中，新请求在忙时会被 STALL。主机不重试有副作用的命令。

| 字段 | 含义 |
| --- | --- |
| `sequence` | 请求序号，响应原样返回，用于关联；不是重放去重标识 |
| `operation` | STATUS / START / RESET / ARRAY_BEGIN / ARRAY_DATA / ARRAY_COMMIT / BYPASS / HCI_ENABLE |
| `argument` | ARRAY_BEGIN 的完整数组字节数 |
| `command` | ARRAY_BEGIN 的 vendor command，0xbb / 0xcc / 0xdd |
| `crc` | ARRAY_BEGIN 的全数组 CRC-16 |
| `offset` | ARRAY_DATA 的字节偏移，必须等于当前已接收长度 |
| `data` | ARRAY_DATA 的数据块，最多 40 字节 |
| `result` | OK / BUSY / INVALID_ARGUMENT / INVALID_STATE / CRC_MISMATCH / VENDOR_ERROR |
| `state` | RESET_HELD=0 / ROM=1 / HCI=2 / ERROR=3 |
| `vendor_result` | SDK 操作的原始返回值，仅 vendor 操作失败时有意义 |

请求中的结果字段不参与执行。响应保留序号及操作，并清空 `data`。
SET_REPORT 只表示请求被 USB 接收；主机必须等待匹配序号的最终响应，BUSY 不是执行成功。
单条命令等待上限 30 秒，轮询间隔 2 ms。超时或 vendor 失败后应复位并从头下载，
不要继续发送剩余步骤。vendor 失败使固件进入 ERROR，必须 START/RESET 后才允许继续。
USB 断开或总线复位会使未完成的上传失效，不自动重启小核。

HCI RX 在 bridge 初始状态关闭。START/RESET 先等待正在执行的 HCI SPI 读取结束，
再关闭 RX；CDC TX 与 HID worker 共用互斥锁。只有 HCI 状态允许将 CDC H4 发往 SPI。
这样 SDK 同步读取的下载应答不会被 HCI RX 任务抢走。JSON 最后一步显式 HCI_ENABLE 后，
才开始正常 H4 通信。

更新 `.proto` 后重新生成（正常构建使用已提交的 `.pb.c/.pb.h`，不要求安装生成工具）：

```sh
python3 third_party/nanopb/generator/nanopb_generator.py \
  -I proto -I third_party/nanopb/generator/proto -D proto proto/bridge.proto
```

生成器需要兼容版本的 Python protobuf 和 protoc（也支持 grpcio-tools 的 protoc）。

## 验证范围

2026-11-02 已完成 macOS 实机 HID 枚举、单步下载、完整启动、HCI 同步和扫描验证，
并修复 CMSIS-DAP 共存时的 CDC 自动发现，记录见 [macOS 板级测试](bridge-hid-macos-test-2026-11-02.md)。
原生测试覆盖 64 字节协议边界、错误报文、CRC、JSON 更换/非法配置，以及固件状态切换、
乱序分块、CRC 不匹配拒绝下发、vendor 失败后必须复位。连接与 GATT 数据收发尚未在本次 bridge 测试中验证。
