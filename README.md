# n32wb452-nimble

N32WB452 上的 Apache NimBLE 工程。闭源 NZ8801 BLE controller（片内协处理器，RivieraWaves 栈）
+ 开源 NimBLE host，三种形态，一个仓库：

```
┌─────────────────────────────── ① nimble 固件 ───────────────────────────────┐
│  N32WB452 M4F: FreeRTOS + NimBLE host ── SPI HCI (H4) ── NZ8801 controller  │
│  广播 NUS 兼容 echo 服务 + 7A02480E 测试服务（uptime/echo/PHY）              │
└──────────────────────────────────────────────────────────────────────────────┘
┌─────────────────────────────── ② bridge 固件 ───────────────────────────────┐
│  N32WB452 M4: USB CDC ACM ⇄ SPI H4；HID 控制小核复位和运行时固件下载     │
└──────────────────────────────────────────────────────────────────────────────┘
┌─────────────────────────────── ③ macOS blectl ──────────────────────────────┐
│  Mac: NimBLE host（linux NPL + Darwin 后端）── /dev/cu.usbmodem* ── ②       │
│  命令行：info / scan / probe（GATT 发现 + NUS 回环测试）                     │
└──────────────────────────────────────────────────────────────────────────────┘
```

①②是**同一个工程**，通过 CMake 选项注入宏切换（`APP_MODE_NIMBLE` / `APP_MODE_BRIDGE`）。

## 硬件背景

- N32WB452：Cortex-M4F @144MHz，512K flash，128K SRAM。
- BLE controller 是片内 NZ8801 协处理器（纯 LE，RivieraWaves 栈，mask ROM ≥620KB），
  通过 SPI1 + 三根 GPIO 与大核相连：
  - RESET（大核→小核，复位/进入下载模式）
  - PD6 ready/wakeup（大核→小核）
  - PE14 data-valid IRQ（小核→大核，EXTI15_10）
- 上电后大核必须先给小核下载固件数组（Copy/Ramcode/Patch/Nvds + 0xEE bypass，见
  `third_party/sdk/n32wb452_ble_driver/platform/Interface.c` 的 `ble_initdata_down()`），
  之后小核才说标准 H4 HCI。
- 闭源驱动 `libn32wb452_ble.a`（IAR 编译，GNU arm-none-eabi 直接链接）提供 SPI/GPIO 原语
  （`eif_spi_send_bytes` / `eif_spi_recv_bytes` / `eif_spi_recv_irq_enable` / …）。

## 目录结构

```
CMakeLists.txt                 MCU 固件构建（模式注入、flash/rtt 目标）
toolchain-arm-gcc.cmake        arm-none-eabi 工具链（自动探测本机路径）
N32WB452_Series.yaml           probe-rs 芯片定义
app/                           固件源码
  main.c                       公共启动：时钟/RTT/FreeRTOS + 模式分发
  app_nimble.c/.h              ① NimBLE host + NUS echo 服务 + 7A02480E 测试服务 + 广播 + vendor 0xFC32
  app_bridge.c/.h              ② CherryUSB CDC ⇄ SPI H4 双向转发
  bridge_control.c/.h          HID 控制任务、小核状态及数组上传
  n32wb452_it.c/.h             EXTI15_10/TIM3/fault dump（SVC/PendSV 由 FreeRTOS 宏映射）
  syscalls.c                   printf→RTT、_sbrk
  FreeRTOSConfig.h / n32wb452_flash.ld / n32wb452_conf.h / log.h
components/
  ble/                         SPI HCI transport（hci_spi.c/ble_spi_tx.c/hci_fixup.c/ble_platform.c）
  nimble_host/                 NimBLE host 静态库 + syscfg 覆盖 + log 桩
  cherryusb/                   CherryUSB device（CDC ACM，bridge 另加 vendor HID）
third_party/                   裁剪引入的依赖（见下）
tools/
  blectl/                      ③ macOS NimBLE host 命令行工具
  macos-ble -> /…/ch32v208-nimble/tools/macos-ble      软链：CoreBluetooth 扫描/连接工具
  android-ble -> /…/ch32v208-nimble/tools/android-ble  软链：Android 测试端
FIX_BOOT_PROMPT.md             启动问题调试交接文档（历史记录）
docs/BOOT_FIX.md               启动修复原因与两种模式的板上验证记录
docs/ble-test-2026-11-02.md    macOS/安卓双平台板级验收测试报告
```

`third_party/` 内容（均从参考工程/官方发布裁剪拷贝）：

| 目录 | 来源 | 说明 |
|---|---|---|
| `sdk/` | Nations.N32WB452_Library 2.5.0 | CMSIS + 标准外设驱动 + BLE driver（含 `lib/IAR/libn32wb452_ble.a`） |
| `mynewt-nimble/` | apache/mynewt-nimble | host + transport + porting(nimble, npl/freertos, npl/linux) + tinycrypt |
| `freertos/` | FreeRTOS-Kernel v202411.00 | 内核 + ARM_CM4F 端口 + heap_4 |
| `segger_rtt/` | SEGGER RTT | 日志通道（终端 0） |
| `cherryusb/` | CherryUSB | usbd core + CDC ACM + fsdev(nations glue) |
| `nanopb/` | nanopb 0.4.9.1 | 固件与 blectl 共用的 protobuf 编解码 |

## 构建

MCU 固件（需 arm-none-eabi-gcc，本机默认取
`/Users/kai/DevTools/arm-gnu-toolchain-13.3.rel1-darwin-arm64-arm-none-eabi`，
可用 `ARM_GCC_PATH` 覆盖）：

```bash
cmake -B build-nimble -GNinja -DN32WB452_APP_MODE=nimble   # ① 默认
cmake --build build-nimble

cmake -B build-bridge -GNinja -DN32WB452_APP_MODE=bridge   # ②
cmake --build build-bridge
```

产物 `build-*/n32wb452_ble_{nimble,bridge}`（+ .bin/.hex/.map）。
当前体积（链接器统计）：nimble 90028 bytes flash / 82360 bytes RAM；
bridge 加入 HID 后约 54 KiB flash / 97 KiB RAM。RAM 包含 FreeRTOS 56 KiB 堆、libc 8 KiB 堆与 MSP 最小预留。

macOS 工具（需要 HIDAPI，例如 `brew install hidapi`；工具专用 JSON 库在 `tools/blectl/third_party`）：

```bash
cmake -S tools/blectl -B tools/blectl/build -GNinja
cmake --build tools/blectl/build
```

## 烧录与日志（probe-rs + CMSIS-DAP）

```bash
# 烧录并复位
ninja -C build-nimble flash          # 或: cmake --build build-nimble --target flash
# RTT 日志（attach 不复位；要看 boot 输出先 attach 再另行 reset）
ninja -C build-nimble rtt            # 或: probe-rs attach --chip-description-path … --chip N32WB452CEQ6 <elf>
```

等效手敲：

```bash
probe-rs download --chip-description-path N32WB452_Series.yaml --chip N32WB452CEQ6 --speed 1000 build-nimble/n32wb452_ble_nimble
probe-rs reset    --chip-description-path N32WB452_Series.yaml --chip N32WB452CEQ6
probe-rs attach   --chip-description-path N32WB452_Series.yaml --chip N32WB452CEQ6 build-nimble/n32wb452_ble_nimble
```

日志走 SEGGER RTT up-channel 0（printf 重定向，见 `app/syscalls.c`）。

## blectl 用法（bridge 固件 + Mac）

bridge 上电将小核保持在复位状态，等待 HID 命令。blectl 的 info/scan/probe 默认先从
运行时 JSON 下载数组并启动小核；可用 `--no-boot` 跳过。完整协议、JSON 格式和单步命令见
[Bridge HID 控制文档](docs/bridge-hid.md)。

```bash
tools/blectl/build/blectl info                                   # 桥端口 + controller 地址
tools/blectl/build/blectl -t 10 scan                             # 扫描
tools/blectl/build/blectl --name N32 -t 10 scan                  # 按名字过滤
tools/blectl/build/blectl --name N32-NimBLE probe                # 连接 + GATT 发现 + NUS 回环
tools/blectl/build/blectl --addr F1:AB:26:68:84:66 -w "00 11 22" probe
tools/blectl/build/blectl --port /dev/cu.usbmodemXXX scan        # 多 CDC 时显式指定
```

macOS 通过 IOKit 查找串口对应 USB 祖先的 VID:PID（`34b7:0002`），不会误选 CMSIS-DAP
调试器的 CDC；多块 N32 bridge 同时连接时需显式指定 `--port` 和 `--hid-path`。
NimBLE 内部日志走 stderr，命令输出走 stdout（便于管道处理）。

## 已内置的 controller 兼容补丁（勿删）

移植自参考工程实机验证过的三个坑（细节见 `components/ble/hci_fixup.c` 与
`components/ble/hci_spi.c` 注释）：

1. **NVIC 优先级组**：`eif_spi_recv_irq_enable()` 内部会把优先级组改成 Group_2，
   破坏 FreeRTOS ISR 安全 → 调用后恢复 Group_4 并重设 EXTI15_10 优先级。
2. **Feature 注入**：纯 LE controller 的 `Read_Local_Supported_Features (0x1003)`
   返回全零、`LE_Read_Local_Supported_Features (0x2003)` 缺 DLE 位 → 在
   EVT Command Complete 上注入（固件侧 `hci_spi.c` 与 blectl 侧 `hci_cdc.c` 都做，幂等）。
3. **Vendor 命令 0xFC32（Set BD Address）** 必须走 `ble_hs_hci_cmd_tx()`（NimBLE 内部 API），
   否则 ACK 队列污染导致后续命令报 `BLE_HS_ECONTROLLER`。

另：闭源驱动要求 `N32WB452_BT_API` 宏（SDK `Interface.c` 的 `#else` 分支编不过），
`components/ble/inc/` 下提供了它依赖的三个胶水头文件。

另外两个实测发现的平台坑（已修复，保持现状勿回退）：

4. **FreeRTOSConfig 不得自定义 `configSYSTICK_CLOCK_HZ`**（哪怕值等于 CPU 时钟）：
   ARM_CM4F port 会因此清 SysTick CLKSOURCE 位选择外部参考时钟，N32WB452 上该
   时钟没跑，tick 冻结为 0（vTaskDelay/callout/软件定时器全瘫，纯事件驱动的 BLE
   不易察觉）。见 `app/FreeRTOSConfig.h` 注释。
5. **NZ8801 不实现 LE PHY HCI 命令**（LE Read/Set PHY 返回 Unknown Command 0x01）：
   测试服务 3E04 特征做优雅降级——读回退 LE 默认 1M、status 字节上报真实 HCI
   错误码。2M/Coded PHY 在此 controller 上不可用，测试工具的 PHY 用例属预期失败。

## 当前状态

- 两种固件均完成构建、烧录与启动验证；NimBLE 广播已由 Mac 扫描确认。
- 原有 CDC bridge 已通过板级验证；本次新增 HID 和主机下载流程已通过构建及原生测试，尚待板级验证。
- macOS / 安卓双平台板级验收（广播/连接/GATT 发现/echo）通过，报告见
  [docs/ble-test-2026-11-02.md](docs/ble-test-2026-11-02.md)。
- 修复原因、历史 HardFault 的复现边界和验证日志见 [docs/BOOT_FIX.md](docs/BOOT_FIX.md)。

## 许可

- 本仓库自有代码：随仓库声明。
- `third_party/` 各自保留上游许可（Apache-2.0 / MIT / SEGGER license / Nations SDK 许可）。
