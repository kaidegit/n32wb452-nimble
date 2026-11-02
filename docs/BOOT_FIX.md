# 启动问题修复与板上验证

验证日期：2026-11-02（Asia/Shanghai）。工具链 ARM GCC 13.3.rel1，CMSIS-DAP + probe-rs。

## 结论与复现边界

两种模式均已构建、烧录并完成启动验收。最终板上保留 NimBLE 固件，Mac 实际扫描到 `N32-NimBLE` 广播。

本次接手时，现有 NimBLE ELF 可以从 Reset_Handler 进入 main、启动 FreeRTOS、完成 NZ8801 固件下载。
GDB 复现的阻塞点是 `app_nimble_start()` 中 `ble_gatts_count_cfg()` 返回 3（`BLE_HS_EINVAL`），随后触发应用断言。
交接记录中“banner 前 HardFault”未在本次基线上复现，不能将历史事件的触发原因认定为已经定位。
原 fault dump 的帧解析和长度限制有确定缺陷，现已修正并用真实异常验证。

## 原因与改动

### 1. NimBLE GATT 服务定义无效

NUS TX 是 notify-only 特征，但 `.access_cb = NULL`。当前 NimBLE 的 `ble_gatts_chr_is_sane()`
要求每个特征都有回调，包含 notify-only 特征。服务计数阶段因此返回 `BLE_HS_EINVAL`，host 任务尚未创建。

在 `app/app_nimble.c` 中增加 TX access callback，返回 `BLE_ATT_ERR_READ_NOT_PERMITTED`。
通知仍通过 `ble_gatts_notify_custom()` 发送，特征权限不变。未修改第三方 NimBLE。

### 2. bridge 的 USB 平台代码未进入最终 ELF

`usb_glue_nations.c` 原来位于 CherryUSB 静态库中。fsdev 内部已有 weak 空初始化函数，
启动文件也有 weak USB 中断别名；这些符号足以满足链接器引用，因此不会拉入平台 glue 对象。

修复前，ELF 中 `usb_dc_low_level_init` 是空实现，两个 USB 中断都指向 `Default_Handler`。
bridge 烧录运行后曾出现 SWD halt 超时；这发生在缺少真实 USB 初始化的构建上，
本次通过 OpenOCD 软件复位恢复了调试连接，未取得超时当时的精确 PC。

将平台 glue 从 CherryUSB 静态库源文件中移出，在 bridge 模式直接加入可执行目标。
修复后 `usb_dc_low_level_init`、`USB_LP_CAN1_RX0_IRQHandler`、`USBWakeUp_IRQHandler`
都是实际的 strong 实现，两个中断的向量表项与对应函数地址精确匹配。

### 3. libc 堆未被作为 RAM 分配

链接脚本中的 `.heap (COPY)` 不带 ELF `ALLOC` 属性，不计入 RAM 使用量；
原 map 中 `.heap` 与 `._user_heap_stack` 都从 `0x200119b4` 开始。
这属于链接布局缺陷，但本次未证明它曾触发历史 HardFault：基线中堆与实际 RAM 顶部 MSP 仍有余量。

改为 `.heap (NOLOAD)`，将 8 KiB libc 堆纳入 RAM 分配，加入堆与 MSP 最小保留空间的 ASSERT。
FreeRTOS 的 56 KiB heap_4 池保持不变。CMake 添加链接脚本依赖，修改脚本后会自动重链接。

最终 map：

| 项目 | nimble | bridge |
| --- | --- | --- |
| 向量表 | `0x08000000`，长度 `0x198` | 相同 |
| `_sidata` | `0x08015e7c` | `0x0800ab9c` |
| `_sdata` / `_edata` | `0x20000000` / `0x20000130` | `0x20000000` / `0x20000078` |
| `_sbss` / `_ebss` | `0x20000130` / `0x200119b4` | `0x20000078` / `0x2001197c` |
| libc 堆 | `0x200119b8`–`0x200139b8` | `0x20011980`–`0x20013980` |
| `_estack` | `0x20020000` | 相同 |
| FLASH / RAM（链接器报告） | 90028 / 82360 bytes | 44052 / 82304 bytes |

`.heap` 现在为 `NOBITS + WA`，`._user_heap_stack` 紧随其后，地址不再重叠。

### 4. 原异常日志解析不可靠且输出被截断

原代码在普通 C 函数内读取 LR/MSP：调用及函数序言可能已改变它们，因而不能可靠取得
异常入口的 EXC_RETURN 和硬件异常帧。原 `snprintf` 的 160-byte 缓冲区也容纳不下全部寄存器输出，
所以“只打印 r0-r3”本身不足以证明发生了嵌套 fault。

改为 naked 汇编入口，在 C 序言前保存 EXC_RETURN、选出 MSP/PSP，再跳转到 C 输出函数。
逐项使用固定宽度十六进制输出，移除 snprintf，强制 RTT 非阻塞；读帧前检查 RAM 边界和 stacking error。
Cortex-M4 的 basic 和 extended FPU frame 均从核心寄存器开始，不跳过 FPU 区域。
该方案仍依赖可用的 MSP 和 RTT RAM；不保证在严重栈破坏或 RTT 控制块损坏时继续输出。

交接记录中的 `HFSR=0x80000000` 是 DEBUGEVT 位；FORCED 是 `0x40000000`。
BFAR/MMFAR 只有在 CFSR 的 BFARVALID/MMARVALID 位设置时才可作为有效故障地址解释。

### 5. RTT 快捷目标

将旧的 `probe-rs rtt ... attach` 改为当前 CLI 支持的 `probe-rs attach ...`，
并设置 `--rtt-channel-mode no-block-skip`，避免调试器把日志通道改成阻塞模式。

## 验证结果

### NimBLE：最终固件 RTT 摘要

```text
=== n32wb452-nimble (nimble) ===
sysclk=144000000 Hz  FreeRTOS heap=57344 bytes
I [main] mode: nimble
I [main] BLE interface init
I [main] BLE hardware init (SPI + reset core)
I [main] downloading NZ8801 firmware...
I [main] NZ8801 firmware download ok
I [hci.spi] transport up (irq_line=0)
I [ble.app] nimble host started
I [ble.app] bdaddr set: 1E:AB:26:68:84:66
I [ble.app] advertising as "N32-NimBLE"
```

Mac CoreBluetooth 扫描 8 秒，精确名称过滤 `N32-NimBLE`，`matched_devices=1`、`failures=0`。
收到可连接广播。此次只验证广播，未进行连接、GATT echo 或性能测试。

### bridge：RTT、USB 枚举与 HCI 同步

```text
=== n32wb452-nimble (cdc-bridge) ===
sysclk=144000000 Hz  FreeRTOS heap=57344 bytes
I [main] mode: cdc-bridge
I [main] BLE interface init
I [main] BLE hardware init (SPI + reset core)
I [main] downloading NZ8801 firmware...
I [main] NZ8801 firmware download ok
I [hci.spi] transport up (irq_line=0)
I [bridge] cdc bridge started
```

Mac 出现 `/dev/cu.usbmodem0000000000021`。
IOUSB 产品名 `N32 BLE HCI Bridge`，VID:PID=`34B7:0002`，configuration=1；GDB 中 `s_usb_configured=true`。
额外通过现有 blectl 验证完整 USB CDC ↔ SPI HCI 通路：

```text
$ tools/blectl/build/blectl --port /dev/cu.usbmodem0000000000021 info
port: /dev/cu.usbmodem0000000000021
own public addr: 66:84:68:26:AB:EF
host synced: yes
```

两种 host 初始化均出现 `Failed to restore IRKs from store; status=8`，随后仍完成同步；此次未测试持久化绑定。

### 异常诊断注入

通过 GDB 向空闲 RAM `0x2001f000` 写入临时 UDF 指令，并在 main / main_task 断点处跳转，
未将注入代码写入项目或 Flash。验证结果：

| 情况 | EXC_RETURN | RTT 中的 PC |
| --- | --- | --- |
| main 中 MSP basic frame | `fffffff9` | `2001f000` |
| main_task 中 PSP basic frame | `fffffffd` | `2001f000` |
| main_task 中先执行 VMOV，再 UDF，PSP extended frame | `ffffffed` | `2001f004` |

三种情况均完整输出到 xpsr。故意注入 UDF 时均得到 `CFSR=00010000`（UNDEFINSTR）、
`HFSR=40000000`（FORCED）。验证结束后已复位并烧录最终 NimBLE 固件，重新确认完整启动与空中广播。

## 复现命令

```sh
cmake -B build-nimble -GNinja -DN32WB452_APP_MODE=nimble
cmake --build build-nimble --target flash
cmake --build build-nimble --target rtt

cmake -B build-bridge -GNinja -DN32WB452_APP_MODE=bridge
cmake --build build-bridge --target flash
cmake --build build-bridge --target rtt
```

RTT attach 不会复位；当前启动摘要不足 1 KiB，立即 attach 可以读到缓冲中的启动日志。
同一探针避免同时运行 GDB、RTT 和 reset/download 进程。

若旧 bridge 导致 probe-rs 无法 halt，本次有效的软件复位恢复命令（不擦除 Flash）：

```sh
openocd -f interface/cmsis-dap.cfg \
  -c 'transport select swd' -c 'adapter speed 1000' \
  -c 'swd newdap n32 cpu -expected-id 0x2ba01477' \
  -c 'dap create n32.dap -chain-position n32.cpu' \
  -c 'target create n32.cpu cortex_m -dap n32.dap' \
  -c 'cortex_m reset_config sysresetreq' \
  -c 'init' -c 'reset halt' -c 'shutdown'
```

保留同一工程的 APP_MODE 切换、FreeRTOS 中断宏映射、闭源 BLE 驱动链接、固件下载链、SPI 时序与兼容 workaround。
