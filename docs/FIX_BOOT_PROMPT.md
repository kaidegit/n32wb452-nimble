# 任务：修复 n32wb452-nimble 固件启动期 HardFault

你接手的仓库：`/Volumes/aigo_1t/GitRepo/n32wb452-nimble`（git 仓库，当前分支干净未提交）。
板子已连接：CMSIS-DAP（CherryUSB 0d28:0204），用 probe-rs 访问。**只修固件启动问题，不要重构其它部分。**

## 你的目标（验收标准）

1. `build-nimble`（nimble 模式）烧录后 RTT 日志能看到完整启动链：
   `=== n32wb452-nimble (nimble) ===` → `BLE interface init` → `BLE hardware init` →
   `downloading NZ8801 firmware...` → `NZ8801 firmware download ok` →
   `nimble host started` → `advertising as "N32-NimBLE"`（host/controller 同步成功）。
2. `build-bridge`（USB CDC 桥模式）同样能启动，Mac 上出现 `/dev/cu.usbmodem*` 设备。
3. 两种模式是**同一个工程**，`N32WB452_APP_MODE` cmake 选项注入 `APP_MODE_NIMBLE`/`APP_MODE_BRIDGE` 宏切换，请保持这个结构。
4. 修好后把改动写入 `docs/BOOT_FIX.md`（原因、改法、验证日志摘要）。

## 背景：这个工程是什么

- N32WB452（Cortex-M4F @144MHz, 512K flash, 128K RAM）+ 片内 NZ8801 BLE 协处理器（闭源 RivieraWaves 栈）。
- 架构：NimBLE host 跑在 M4 上，通过 SPI1 + 3 根 GPIO（RESET / PD6 ready / PE14 data-valid IRQ）与 NZ8801 说话，HCI 为 H4 over SPI。
- 闭源部分：`third_party/sdk/n32wb452_ble_driver/lib/IAR/libn32wb452_ble.a`（IAR 编译，GCC 直接链接，参考工程已验证可行）+ SDK `platform/Interface.c`（固件下载链 Copy/Ramcode/Patch/Nvds 四个数组 + 0xEE bypass）。
- 全部知识来自参考工程 `/Volumes/aigo_1t/GitRepo/OfflineProgrammerForEsp`（名字不要引入本仓库），重点文档：
  - `firmware/OfflineProgrammerForEsp_n32wb452/applications/components/ble/documents/PORTING_GUIDE.md`（三个已知坑：NVIC 优先级组被 vendor 代码篡改成 Group_2；纯 BLE 控制器 LMP features 全零需要注入 LE 位；vendor HCI 命令必须走 ble_hs_hci_cmd_tx 否则 ACK 队列污染）
  - `docs/BLE_DEBUG_HANDOFF.md`（传输层/下载协议完整规格）
  - 参考工程的 bootloader（`firmware/bootloader_n32wb452`）是裸机 CMake 工程，同一块板、同一 SDK 启动文件，能正常跑——你的 linker/启动配置可以对照它。

## 构建与烧录命令（在仓库根目录）

```bash
# nimble 模式（默认）
cmake -B build-nimble -GNinja -DN32WB452_APP_MODE=nimble && cmake --build build-nimble
# bridge 模式
cmake -B build-bridge -GNinja -DN32WB452_APP_MODE=bridge && cmake --build build-bridge
# 烧录（elf 直接）
probe-rs download --chip-description-path N32WB452_Series.yaml --chip N32WB452CEQ6 --speed 1000 build-nimble/n32wb452_ble_nimble
probe-rs reset  --chip-description-path N32WB452_Series.yaml --chip N32WB452CEQ6
# RTT 日志（attach 不会复位，先 attach 再另开 terminal 执行 reset 才能抓到 boot 输出）
probe-rs attach --chip-description-path N32WB452_Series.yaml --chip N32WB452CEQ6 build-nimble/n32wb452_ble_nimble
# GDB（默认监听 127.0.0.1:1337）
probe-rs gdb --chip-description-path N32WB452_Series.yaml --chip N32WB452CEQ6
#   然后: arm-none-eabi-gdb -ex 'target extended-remote 127.0.0.1:1337' ...
#   工具链: /Users/kai/DevTools/arm-gnu-toolchain-13.3.rel1-darwin-arm64-arm-none-eabi/bin/
# 也可用 cmake 快捷目标: ninja -C build-nimble flash / rtt
```

## 已知的当前症状（已做过两轮排查）

1. **第一轮**：`*** PANIC: FreeRTOS assert ***` 在调度器启动时。原因：我在 `app/n32wb452_it.c` 写了
   `SVC_Handler(){vPortSVCHandler();}` 包装函数，而 FreeRTOS ARM_CM4F port 会断言向量表槽位
   **精确等于** `vPortSVCHandler`/`xPortPendSVHandler`/`xPortSysTickHandler`。
   **已修复**：改为 `app/FreeRTOSConfig.h` 里的标准宏映射（`#define vPortSVCHandler SVC_Handler` 等），
   并删除了包装函数。这一步是对的，不要回退。
2. **第二轮（现状）**：启动即 HardFault，早于第一行 banner（printf→RTT 之前）。fault dump 打印了部分寄存器：
   ```
   CFSR=00000000 HFSR=80000000 (FORCED) BFAR=e000ed38
   r0=20020000 r1=0800eb45 r2=080017d9 r3=080017e5
   ```
   解析（addr2line）：
   - `r1=0x0800eb45` → Reset_Handler 内部（说明 fault 发生在启动早期，甚至早于 main）
   - `r2=0x080017d9` → NMI_Handler 入口+thumb 位；`r3=0x080017e5` → HardFault_Handler 入口+thumb 位
     —— 这两个值**恰好等于 .isr_vector 向量表里的表项**，强烈暗示某段代码把向量表当数据表/函数指针表用了，
     或者 fault 帧本身来自嵌套 fault。
   - `r0=0x20020000` = `_estack`（RAM 顶端）也可疑。
   - dump 只打印出 r0-r3 三行就没了：可能是 fault_dump 自身嵌套 fault（RTT 写入/snprintf 在 fault 上下文里），
     也可能只是 RTT attach 时序。**fault_dump 在 `app/n32wb452_it.c`，可自行加固。**

## 排查假设（按可能性排序，欢迎推翻）

1. **链接脚本问题**：`app/n32wb452_flash.ld` 是我照参考 bootloader 改的，但
   a) 加了 `.heap (COPY)` 8K 段（`__HeapBase/__HeapLimit` 给 newlib _sbrk，见 `app/syscalls.c`）；
   b) 没有像 bootloader 那样 `/DISCARD/ libc/libm/libgcc`（我们需要 printf，所以保留了）。
   对照 `build-nimble/n32wb452_ble_nimble.map` 检查 `_sidata/_sdata/_edata/_sbss/_ebss/_estack`、
   .isr_vector 位置、.heap 是否合法落在 RAM。
2. **启动文件**：`third_party/sdk/CMSIS/device/startup/startup_n32wb452_gcc.s`（vendor 原版）。
   检查它期望的符号是否都被 linker 提供（比如它可能引用 `__libc_init_array`、`SystemInit`、`_estack`、
   或者在 Keil/IAR 语义下写的段名与 GNU ld 不匹配——bootloader 用同一个文件能跑，但要确认我 ld 里
   `.isr_vector` 段的 KEEP 和位置没有把它后面的内容挤掉）。
3. **SystemInit**（`third_party/sdk/CMSIS/device/system_n32wb452.c`，144MHz HSE PLL）在 Reset_Handler 里被调。
   若 HSE 未起振会死等而不是 fault；但请确认它没在被 FreeRTOS 改 NVIC 分组之前就配置向量表/FPU。
4. **FPU/ABI**：全工程 `-mfloat-abi=hard -mfpu=fpv4-sp-d16`。bootloader 相同。IAR 闭源库混链在参考工程可行。
5. **RTT 控制块**：`third_party/segger_rtt`（来自用户的 ch32v208 工程，V7.x）首次写入会 lazy init；
   banner 都没出现，可能是 printf/`_write`/RTT 首用路径 fault（`app/syscalls.c`）。
   可以临时在 main() 最开头直接 `SEGGER_RTT_WriteString(0, "A\r\n")` 二分定位。
6. **FreeRTOSConfig**：`app/FreeRTOSConfig.h`。第二轮已经把向量表问题修掉；若仍 fault，
   注意 `configASSERT` 的位置（xPortStartScheduler 里还有 ucMaxSysCallPriority 断言，5<<4=0x50 应该合法）。

## 建议的调试路径

1. GDB 单步 `Reset_Handler`：`probe-rs gdb` + `arm-none-eabi-gdb`，`b Reset_Handler` 后 `monitor reset`，单步找到 fault 指令的精确 PC，`addr2line` 解析。
2. 或者先做最小化：临时把 `main()` 内容替换成纯 RTT 打印循环（不建任务不起调度器）——如果这样能跑，
   问题在 FreeRTOS 启动/任务栈；如果也 fault，问题在更早的启动/链接。
3. 对照参考 bootloader 的 `main/n32wb452_flash.ld` 和启动配置逐段 diff。

## 工程地图

```
CMakeLists.txt                 # MCU 固件（模式注入在这里）
app/                           # main.c / app_nimble.c / app_bridge.c / n32wb452_it.c / syscalls.c / FreeRTOSConfig.h / n32wb452_flash.ld
components/ble/                # hci_spi.c(SPI HCI transport) ble_spi_tx.c hci_fixup.c ble_platform.c + inc/ 胶水头
components/nimble_host/        # NimBLE host 静态库 + inc/syscfg 覆盖
components/cherryusb/          # CDC ACM
third_party/sdk|mynewt-nimble|freertos|segger_rtt|cherryusb
tools/blectl/                  # macOS 端 NimBLE host CLI（已编译可用，bridge 起来后做 E2E）
tools/macos-ble, tools/android-ble  # 软链，CoreBluetooth 扫描工具（验证广播用）
```

## 重要约束

- 不要动 `third_party/mynewt-nimble`（除已打的 os_types.h __APPLE__ 补丁外）。
- `libn32wb452_ble.a` 必须保留链接（闭源 controller 驱动，无可替代）。
- log 走 SEGGER RTT（printf 重定向在 `app/syscalls.c`），不要改成 UART。
- 固件下载链（Interface.c）与 HCI transport（components/ble/hci_spi.c）的时序行为已经过参考工程验证，
  移植时保留了全部关键 workaround（NVIC Group_4 恢复、feature 注入、vendor 命令走 host 内部 API），
  除非证明它们是 fault 来源，否则不要改。
