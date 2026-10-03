# Arduino Uno 与 ESP32-S3 当前引脚接线表

核对日期：2026-10-01。依据当前 `UnoCatController.ino` 与 `ESP32CatHost.ino` 源码。
适用硬件：Arduino Uno R3、Seeed Studio XIAO ESP32-S3 / Sense（当前摄像头引脚配置）。其他 ESP32 板型不能直接套用这张表。

## Arduino Uno R3：逐引脚分配

| Uno 引脚 | 应连接的器件端子 | 功能与说明 |
| --- | --- | --- |
| D0 / RX | ESP32 D6 / GPIO43 / TX，经 UART 电平接口 | 接收 ESP32 指令，57600 波特；与 Uno USB 串口共用 |
| D1 / TX | 经 5V→3.3V 电平转换接 ESP32 D7 / GPIO44 / RX | 向 ESP32 返回状态、ACK、事件；不可把 5V TX 直接接 ESP32 RX |
| D2 | 左轮光槽编码器 DO | INPUT_PULLUP，下降沿计数；当前设定 20 脉冲/车轮圈 |
| D3 | 右轮光槽编码器 DO | INPUT_PULLUP，下降沿计数 |
| D4 | L298N IN1 | 右轮方向控制 1 |
| D5 / PWM | L298N ENA | 右轮 PWM 调速，取下 ENA 短接帽 |
| D6 / PWM | L298N ENB | 左轮 PWM 调速，取下 ENB 短接帽 |
| D7 | L298N IN2 | 右轮方向控制 2 |
| D8 | L298N IN3 | 左轮方向控制 1 |
| D9 | L298N IN4 | 左轮方向控制 2 |
| D10 | 左侧 HC-SR04 TRIG | 发射触发，传感器朝左侧 |
| D11 | 左侧 HC-SR04 ECHO | 左侧回波输入 |
| D12 | 有源蜂鸣器模块 SIG / 驱动电路控制端 | HIGH 触发；裸蜂鸣器或超过 IO 电流能力的负载用晶体管驱动 |
| D13 | 板载 LED；或外接 LED 限流电阻 | 灯光输出；外接 LED 另一端接 GND |
| A0 | 顶部触碰开关 TOP | 开关另一端接 GND；INPUT_PULLUP，按下 LOW |
| A1 | 左侧触碰开关 LEFT | 同上 |
| A2 | 右侧触碰开关 RIGHT | 同上 |
| A3 | 后方触碰开关 REAR | 同上 |
| A4 | 右侧 HC-SR04 TRIG | 当前作为数字 IO；不用于 I2C SDA |
| A5 | 右侧 HC-SR04 ECHO | 当前作为数字 IO；不用于 I2C SCL |
| GND | ESP32、传感器、驱动板 GND 与电机电源负极 | 所有设备共地 |
| 5V | 两个 HC-SR04 VCC；其他已确认支持 5V 的模块供电 | 不给电机供电；编码器和蜂鸣器供电按实物规格 |
| 3.3V / AREF / ICSP | 当前无外接器件分配 | 不需要额外接线 |
| USB 接口 | 电脑（烧录或单独测试 Uno） | 57600 波特；USB 串口与 D0/D1 为同一个命令通道 |

电机不接 Uno IO：左电机两线接 L298N OUT3/OUT4，右电机两线接 OUT1/OUT2。如果实际正转方向相反，交换对应电机两线。L298N 电机电源接独立且符合额定值的电源；逻辑 5V 和稳压跳线按实际模块说明配置，避免多个 5V 输出互相供电。

## ESP32-S3：外部接线与当前用途

表中 D 编号是 XIAO 板上编号；GPIO 编号是 ESP32 芯片编号。不要把 ESP32 D6 与 Uno D6 混淆。

| XIAO 引脚 / 接口 | 芯片 GPIO | 应连接的器件 | 当前用途 |
| --- | --- | --- | --- |
| D6 / TX | GPIO43 | Uno D0 / RX，经 UART 电平接口 | 板间 UART 发送，57600 波特 |
| D7 / RX | GPIO44 | Uno D1 / TX，经 5V→3.3V 电平转换 | 板间 UART 接收，57600 波特 |
| GND | — | Uno GND、UART 电平转换模块 GND | 共地 |
| 3V3 | — | 电平转换模块低压侧参考电源（如模块需要） | 3.3V 侧供电；不接 Uno 5V |
| USB-C | 原生 USB | 电脑 | ESP32 控制与日志，Serial 配置为 115200；与 GPIO43/44 的 UART 独立 |
| 摄像头接口 / Sense 扩展板 | 见下表 | 已配套的摄像头与扩展板 | 图像采集；使用原连接器，不按表自行猜排线针脚顺序 |
| D0 / A0 | GPIO1 | 暂不接 | 当前应用未分配 |
| D1 / A1 | GPIO2 | 暂不接 | 当前应用未分配 |
| D2 / A2 | GPIO3 | 暂不接 | 当前应用未分配；新增用途前核对启动配置 |
| D3 / A3 | GPIO4 | 暂不接 | 当前应用未分配 |
| D4 / SDA | GPIO5 | 暂不接 | 当前应用未分配 |
| D5 / SCL | GPIO6 | 暂不接 | 当前应用未分配 |
| D8 / SCK | GPIO7 | 暂不接 | 当前应用未分配；Sense 扩展板 SD 卡可能使用 |
| D9 / MISO | GPIO8 | 暂不接 | 当前应用未分配；Sense 扩展板 SD 卡可能使用 |
| D10 / MOSI | GPIO9 | 暂不接 | 当前应用未分配；Sense 扩展板 SD 卡可能使用 |
| 5V | — | 当前无需连接 Uno 5V | 当前可由 USB 供电；独立供电按开发板规范，不并接两块板的 USB 5V |

“当前应用未分配”不等于整块板硬件绝对空闲；增加器件时还需核对扩展板占用。

## ESP32 摄像头内部信号分配

以下来自当前 `initCamera()` 配置，用于核对摄像头接口；不是摄像头排线的物理针脚序号。名称 D0–D7 在此表示摄像头数据位，不能与 XIAO 外部 D0–D7 编号混用。

| 摄像头信号 | ESP32 GPIO | 说明 |
| --- | --- | --- |
| 数据 D0 | 15 | 8 位并行图像数据 |
| 数据 D1 | 17 | 同上 |
| 数据 D2 | 18 | 同上 |
| 数据 D3 | 16 | 同上 |
| 数据 D4 | 14 | 同上 |
| 数据 D5 | 12 | 同上 |
| 数据 D6 | 11 | 同上 |
| 数据 D7 | 48 | 同上 |
| XCLK | 10 | 摄像头输入时钟，配置为 20 MHz |
| PCLK | 13 | 像素时钟 |
| VSYNC | 38 | 帧同步 |
| HREF | 47 | 行有效信号 |
| SCCB SDA | 40 | 摄像头配置数据 |
| SCCB SCL | 39 | 摄像头配置时钟 |
| PWDN | 未分配（-1） | 固件不通过独立 GPIO 控制 |
| RESET | 未分配（-1） | 固件不通过独立 GPIO 控制 |

## 两块板的 UART 接线与 USB 联调

| 信号方向 | 接线 |
| --- | --- |
| ESP32 → Uno | ESP32 GPIO43 / D6 / TX → UART 电平接口 → Uno D0 / RX |
| Uno → ESP32 | Uno D1 / TX → 5V→3.3V UART 电平转换 → ESP32 GPIO44 / D7 / RX |
| 地线 | ESP32 GND ↔ Uno GND ↔ 转换模块 GND |
| 转换模块电源 | 如使用双电源转换模块：低压侧参考 3.3V、高压侧参考 Uno 5V；按模块端子说明接线 |

推荐使用明确支持 3.3V↔5V、57600 波特 UART 的电平转换接口。ESP32 TX→Uno RX 的方向也通过电平接口，避免把 3.3V 输出是否满足 5V 接收端阈值当成已验证结论。

ESP32 的 USB 控制通道和板间 UART 是独立通道，能同时使用。Uno 的 USB 串口和 D0/D1 共用同一硬件 UART；电脑与 ESP32 不能作为两个独立主机同时控制 Uno。仅在电脑程序中停止发送，也不代表 USB 转串口芯片已电气隔离。

首次联调流程：

1. 断开板间 TX/RX，先完成 Uno 固件烧录。
2. 断电后接好 UART、电平转换与共地。先关闭电机电源或断开电机输出。
3. 直接 UART 控制 Uno 时，优先断开 Uno USB 数据连接，按板卡规范独立供电；ESP32 USB 保留给电脑控制与日志。
4. ESP32 上电默认 `SIM_UNO`。在 ESP32 页面切换为 `UART`，才会收发真实 Uno 通信。
5. 先验证 Uno 状态回报和指令 ACK，再验证电机。重新给 Uno 烧录前断开外部 TX/RX。

## 核对来源与验证范围

- Uno 引脚常量：[UnoCatController.ino](firmware/UnoCatController/UnoCatController.ino)。
- ESP32 UART 与摄像头初始化：[ESP32CatHost.ino](esp32-host/ESP32CatHost/ESP32CatHost.ino)。
- [Seeed 官方 XIAO ESP32-S3 引脚说明](https://wiki.seeedstudio.com/xiao_esp32s3_getting_started/)。
- [Arduino Uno R3 官方原理图](https://www.arduino.cc/en/uploads/Main/Arduino_Uno_Rev3-schematic.pdf)。

本表描述当前源码的预期接线，不表示所有实物接线已验收。ESP32 摄像头初始化与出帧已有记录；Uno 与 ESP32 的真实 UART 往返、全部传感器和实体电机仍需现场验证。摄像头只接 ESP32，前向判定由 ESP32 经串口传给 Uno，没有单独的 Uno 摄像头引脚。

## 电机最低驱动 PWM（2026-10-01 实测）

用户现场测试：当前底盘电机需要至少 PWM 120（0–255）才能启动。Uno 固件常量 `MOTOR_MIN_DRIVE_PWM=120`：物理输出中非零 PWM 的绝对值限制在 120–255，保持正反转符号；PWM=0 仍停止，关闭的物理电机通道仍不输出。实际输出 PWM 的遥测反映限制后的值。该值适用于当前电机、驱动与供电条件。
