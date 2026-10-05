# Uno V6.11：Arduino IDE 编译参数更新（2026-10-05）

当前版本必须使用仓库 platform.local.txt 的完整优化参数，包含新增的 -fno-inline-small-functions。旧参数即使已生效，也会把当前 V6.11 编译为 **32310 bytes**，仍超过 32256；完整参数编译为 **32244 bytes**，静态 RAM **1381 bytes**。本轮已检查电脑上的 AVR 1.8.8 配置，旧文件缺少该项，更新前保留备份；安装后的不带额外 build-property 编译结果见项目日志。

配置路径仍为 C:\Users\guoto\AppData\Local\Arduino15\packages\arduino\hardware\avr\1.8.8\platform.local.txt。Arduino IDE 的 arduino-cli daemon 已运行时需重新加载核心配置，最简单是关闭并重新打开 IDE；本轮不关闭用户编辑窗口。不能把超过容量的 HEX 强行写入或提高板子的 maximum_size。

以下保留历史说明；本版容量与参数以上述说明及当前仓库配置为准。

# Uno V6.6：Arduino IDE 编译与用户手动上传

2026-10-03。Arduino AVR Boards 1.8.8 默认参数将当前完整固件编译为33828 bytes，超过Uno应用区32256 bytes。原有离线脚本使用以下优化参数，完整固件编译为32084 bytes、静态RAM1379 bytes；没有删除功能，Flash剩172 bytes，运行稳定性仍待实测。

| 参数 | 用途 |
|---|---|
| -mcall-prologues | 共用函数进入/退出代码，减少重复 |
| -mrelax | 允许缩短跳转/调用 |
| -fno-split-wide-types | 调整宽类型变量的编译策略，在本构建中减小体积 |
| -finline-limit=3 | 限制函数代码在调用处展开 |
| -Wl,--relax | 最终链接阶段应用指令缩短 |

## 方案一：继续用Arduino IDE（配置一次）

Arduino支持使用platform.local.txt覆盖同目录platform.txt的属性，见[官方规范](https://docs.arduino.cc/arduino-cli/platform-specification/)。本仓库已准备[platform.local.txt](./ide-avr/platform.local.txt)。助手没有修改电脑上的Arduino配置，也没有进行界面/上传/复位操作。

1. 用户关闭Arduino IDE，把上述文件复制到：

   `C:\Users\guoto\AppData\Local\Arduino15\packages\arduino\hardware\avr\1.8.8\platform.local.txt`

   文件名必须是platform.local.txt，不要变成platform.local.txt.txt。本轮检查该位置没有现存文件；若操作时已有同名文件，先保留备份，再合并三个extra_flags设置，不覆盖别的自定义配置。
2. 用户重新打开IDE，打开 `uno-web-lab/firmware/UnoCatController/UnoCatController.ino` 或同步的 `uno-firmware/UnoCatController/UnoCatController.ino`，同时保留同目录scan_machine.h。顶部注释和启动回报均为UNO_CAT_V6.6。
3. 选择Arduino Uno，先点击验证。日志应带上述参数，成功大小约32084 bytes而非33828。若仍用旧参数，先核对文件位置与IDE是否已重启，不改板型、不虚增最大程序容量。
4. 用户自行选择Uno端口并上传。完成后由用户处理RESET，板载网页Real UART点击PING；应收到PONG及EVENT CAPABILITIES SCAN_V1，再开始现场验收。助手不点击上传或RESET。

这个配置作用于该版本Arduino AVR核心下的其他AVR板，不影响ESP32。恢复原始编译设置可由用户移走platform.local.txt并重启IDE。升级AVR核心后路径变化，需要重新放置。当前只能确认命令行同参数离线编译成功，IDE重启/验证/上传仍由用户操作并确认；编译通过不证明上传和实际运动。

## 方案二：不改IDE配置

在uno-web-lab根目录由用户运行：

```powershell
.\firmware\build-uno.ps1
```

脚本仅编译，输出应用HEX在 `$env:TEMP\codex-uno-scan-build\UnoCatController.ino.hex`，保留所有功能。若随后按IDE普通上传按钮而没有配置方案一，IDE会重新用默认参数编译，仍会过大。

已有 `firmware/upload-uno-no-reset.ps1` 可直接写入上述应用HEX，但需要用户确认端口、关闭占用串口的窗口并在运行前手动RESET进入短暂bootloader窗口；示例（COM10仅示例，替换为真实Uno端口）：

```powershell
.\firmware\upload-uno-no-reset.ps1 -Port COM10 -ManualBootloaderReady
```

该脚本使用AVRDUDE8的noautoreset、一次连接尝试，不写bootloader/fuses。若错过窗口或上传失败，保持停止，用户处理恢复。不要把with_bootloader HEX当本次应用HEX使用。

2026-10-03 更新：Uno V6.7 修复扫描停稳计时旧时间戳问题；相同优化参数编译32162 bytes，静态RAM1379 bytes，Flash剩94 bytes。顶部注释与READY均为V6.7。ESP V0.14新增独立停车后识别等待时间设置。原V6.6数据仅为历史构建记录。

2026-10-03 Uno V6.8：相同IDE优化参数编译32206 bytes、RAM1379 bytes。新增同编号/代次SR补确认路径，避免重试重置已开始扫描；ESP配对V0.16。原V6.6/V6.7容量数据为历史记录。
