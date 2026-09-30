# Arduino Uno 固件：UnoCatController

本目录是 2026-09-30 的 Git 源码快照；原工作目录为 ../../uno-firmware。两个 .ino 文件按字节复制并校对，编译产物不入库。后续固件改动须同步本目录并与网页一起提交。仓库根目录可用 arduino-cli compile --fqbn arduino:avr:uno firmware/UnoCatController 编译主固件。

V5.6 新增硬件实时读数：默认打开“硬件测试”，桌面采用四列元件卡片，开关与读数同卡显示。约每 250 ms 请求一次 GET_TELEMETRY，同一时刻最多一个请求，响应丢失时退避；不依赖模拟场景是否暂停。三组互动策略与共享日志默认折叠。

左/右电机显示接收的带符号 PWM、实际引脚驱动记录、方向引脚电平及输出开关；编码器显示累计脉冲、约 500 ms 窗口的 RPM、采样瞬间 HIGH/LOW 和物理/虚拟来源。超声波分别显示物理测距和最终控制读数，触碰显示原始电平及去抖状态，LED/蜂鸣器显示逻辑指令与引脚状态。前向判定标注电脑模拟或上位机来源。超过 2.5 秒未更新的单项读数标为过期；断开或 Uno 重启时清空旧值。数字电平不是波形，电机引脚驱动也不是实测电压、电流或转动反馈；真实转动需要看编码器。ENA/ENB 跳线固定使能时，显示 PWM 不代表实际调速；开启通道也不意味着检测到模块已接入。

使用本次页面前，需重启本地测试台服务器以加载新模块路由，刷新网页，并重新烧录主固件 UnoCatController.ino（READY UNO_CAT_V5.6）；独立的 SimpleMotorTest.ino 只接受 A/B/C/D/S，不能提供网页实时协议。界面操作及硬件验证由用户完成。

V5.6 将物理 IO 拆为 14 个独立通道。原空板准备按钮改为“关闭所有物理输出”，只关闭输出且保留输入选择；另有“准备元器件测试（模拟未接输入）”初始化按钮。完整接线和单轮测试步骤见 [元器件接线与电脑测试](../元器件接线与电脑测试.md)。

固件在 [`UnoCatController/UnoCatController.ino`](./UnoCatController/UnoCatController.ino)。这是 **Arduino Uno 固件，不是 ESP32 固件**。Uno 执行电机、灯、蜂鸣器、触碰、编码器、左右超声波读取和本地安全控制；它没有接摄像头，也不接收、解码或识别图像。固定巡游路线、路线标签识别、当前可见猫观测、互动策略以及寻找猫的开始/停止均由上位机负责。现有 [`uno-web-lab`](../README.md) 网页可暂时充当上位机；其“本地模拟”模式仍是 JavaScript 模型，只有“串口连接”模式才向本固件发指令。

未来的真实系统由 **ESP32-S3 接 OV 摄像头**采集并处理画面：从图像中判断前方能否通行、识别路线标签和猫，再经串口发送 `CAMERA_FRONT` 判定及运动命令给 Uno。摄像头的具体 OV 型号、采集驱动、图像识别和 ESP32 串口程序目前尚未实现。可选的 HTTP 视频流仅供网页预览，不会自动生成避障判定；Uno 需要 ESP32 另外发送有效且持续更新的 `CAMERA_FRONT` 输入才允许前进。

当前固件启动时回报 `READY UNO_CAT_V5.6`。若串口日志显示旧版本，Uno 尚未烧录本版固件。

V5.1 加固了串口接收：`ack`/`done`/`error` 等输出原语在 `Serial.print` 等待发送缓冲空出的间隙继续装配主机命令行（`pumpSerialIn()`，重入时完整行入 1 格软件队列，`loop()` 在当前命令返回后立即派发）。此前上位机在同一毫秒连发“传感器注入 + ROTATE/DRIVE”时，TX 忙等会让 64 字节硬件接收环溢出丢字节，把突发末尾的逃跑指令打成 `UNKNOWN_OR_INVALID_COMMAND`。上位机现已同步加入逐行节流，两侧配合消除该故障；网页空板自动控制要求启动握手为 `READY UNO_CAT_V5.6`。

## 硬件和接线

目标板：普通 **Arduino Uno R3**。所有地线必须共地。L298N 电机供电按电机与驱动板额定值接线，**不要用 Uno 的 5V 引脚直接给电机供电**。用 PWM 调速时取下 L298N 的 ENA/ENB 短接帽。若未来以 ESP32 连接 Uno，Uno TX 的 5V 串口信号需要电平转换后才能接 ESP32 RX；D0/D1 与 Uno 的 USB 串口共用，调试时不要同时让 ESP32 和 USB 主机驱动 Uno RX。

| 功能 | Uno 引脚 | 接线说明 |
| --- | --- | --- |
| 左光槽传感器数字输出 | D2 | 外部中断；默认 `INPUT_PULLUP`，下降沿计数 |
| 右光槽传感器数字输出 | D3 | 外部中断；默认 `INPUT_PULLUP`，下降沿计数 |
| L298N 左轮 IN1 / IN2 / ENA | D4 / D7 / D5 | D5 PWM |
| L298N 右轮 IN3 / IN4 / ENB | D8 / D9 / D6 | D6 PWM |
| 左 HC-SR04 TRIG / ECHO | D10 / D11 | 左侧避障；朝机器人左侧安装 |
| 右 HC-SR04 TRIG / ECHO | A4 / A5 | 右侧避障；朝机器人右侧安装，A4/A5 用作数字引脚 |
| 5V 有源蜂鸣器控制 | D12 | 若模块电流超过 Uno 引脚能力，需用晶体管驱动 |
| LED | D13 | 默认板载 LED；外接 LED 需限流电阻 |
| TOP / LEFT / RIGHT / REAR 触碰开关 | A0 / A1 / A2 / A3 | 开关接地，`INPUT_PULLUP`；按下为 LOW |
| USB/上位机串口 | D0 / D1 | 57600 波特，换行结束 |

左右轮各有一个光槽传感器。光栅盘位于**车轮轴**，每圈 **20 槽**；固件只计每个槽的一次下降沿，即 **20 脉冲/车轮圈**。单通道传感器不能单独判定转向，方向仅取自电机命令。`GET_ENCODER` 返回累计脉冲和最近约 500 ms 的无符号 RPM。供电、有效电平以及左右电机正反方向需按实际模块检查；如果正方向反了，交换对应电机两根线，或调整代码中的方向映射。

[底盘商品页](https://www.tinytronics.nl/en/mechanics-and-actuators/kits/car-kit-diy-round)列出两只轮子和两只测速光栅盘，整车外形尺寸为 150×140×80 mm。[配套车轮商品信息](https://www.tinytronics.nl/en/mechanics-and-actuators/parts/wheels)标称直径 **65 mm**；固件将其作为可修改的初值。**左右轮中心距尚未测量**，不能从整车宽度推断。默认 `TRACK_MM=0`，故角度命令 `ROTATE` 会返回 `TRACK_NOT_SET`。测量轮中心距后用 `SET_GEOMETRY <wheel_mm> <track_mm>` 设置，设置仅在当前通电期间有效。轮胎压缩和打滑也会影响里程；距离/角度回报始终标 `ESTIMATED`。

## 安装与编译

用 Arduino IDE 打开 `UnoCatController/UnoCatController.ino`，板卡选 **Arduino Uno**。当前电脑上的 Arduino CLI 1.5.1 位于 Arduino IDE 安装目录，未加入此会话的 `PATH`；PowerShell 可使用：

```powershell
& 'D:\software\arduinoIDE\Arduino IDE\resources\app\lib\backend\resources\arduino-cli.exe' compile --fqbn arduino:avr:uno .\uno-firmware\UnoCatController
```

编译只检查代码和板卡依赖；烧录和真实接线验证由使用者在方便连接硬件时完成。CLI 需要安装 `arduino:avr` 平台。上电后电机输出为 0。前进需要新鲜的 `CAMERA_FRONT` 输入；转向需要相应侧的超声波有效读数。网页不会把模拟摄像头读数送入 LIVE 模式，因此未接真实摄像头数据源时，实车前进会被拒绝。

## 串口协议

每条命令一行：`CMD <1..65535> <VERB> [参数...]`。立即接受时回 `ACK <id>`；瞬时命令随后回 `DONE <id> [结果]`。有时长/距离的运动用 `EVENT MOTION_STARTED <id> ...` 开始，用 `EVENT MOTION_DONE <id> [ESTIMATED] LEFT_PULSES=<n> RIGHT_PULSES=<n>` 结束，或 `EVENT MOTION_ABORTED <id> <原因> LEFT_PULSES=<n> RIGHT_PULSES=<n>` 中止；脉冲数从该运动开始计。拒绝时回 `ERROR <id> <原因>`。传感器回报用 `DATA`，触碰变化用 `EVENT PHYSICAL TOUCH ...` 或 `EVENT DEBUG_INJECT TOUCH ...`。所有命令大写。

| 命令 | 输入含义 | 输出或约束 |
| --- | --- | --- |
| `PING` | 连通性 | `DONE ... PONG` |
| `SESSION_START` / `SESSION_STOP` | 开始 / 结束整段互动 | 结束时立即停电机、关灯与蜂鸣器；必须重新开始才能运动 |
| `STOP` | 立即停止当前运动 | 中止运动，回 `MOTOR_STOPPED` |
| `MOVE FORWARD\|BACKWARD <pwm> <ms>` | `pwm=1..255`, `ms=1..10000` | 按时间运行；不承诺位移 |
| `TURN LEFT\|RIGHT <pwm> <ms>` | 原地定时旋转 | 不承诺角度 |
| `DRIVE <cm> <pwm>` | `cm=-100..100`，非零 | 双轮分别达到目标光槽计数后停止；最多 10 秒 |
| `ROTATE <degrees> <pwm>` | `degrees=-360..360`，正数向右 | 需要设置轮距；按车轮计数近似旋转；最多 10 秒 |
| `MOTOR <left_pwm> <right_pwm>` | 每轮 `-255..255` | 连续电机控制；每条指令只生效 500 ms，须续发；`MOTOR 0 0` 停止 |
| `LED ON\|OFF\|FLASH <interval_ms> <duration_ms>` | 灯控制 | 闪烁间隔 20..5000 ms、总时长 20..10000 ms |
| `BEEP <ms>` | 有源蜂鸣器 | 1..5000 ms |
| `GET_STATUS` / `GET_DISTANCE` / `GET_ENCODER` / `GET_CONFIG` | 读取状态、距离、累计编码器与速度、几何参数 | `DATA <id> ...` 后 `DONE <id>` |
| `CAMERA_FRONT <CLEAR\|BLOCKED\|INVALID\|0..400>` | ESP32 发送前方障碍判定；若经标定可给出最近障碍距离（cm） | `CLEAR`/`BLOCKED` 分别对应安全/阻挡，`INVALID` 或 500 ms 未更新则禁止前进；`GET_DISTANCE` 中的 `DISTANCE` 为此前向值 |
| `SET_GEOMETRY <wheel_mm> <track_mm>` | 轮直径 20..150 mm、轮中心距 50..300 mm | 当前通电周期有效；运动中不能修改 |
| `SET_SAFE_DISTANCE LEFT\|RIGHT <cm>` | 设置对应侧转向所需的最小超声波净空，5..100 cm，默认各 5 cm | 空闲时可设；成功回 `DONE <id> SAFE_DISTANCE_SET`，运动中回 `BUSY`；`GET_CONFIG` 回 `SAFE_LEFT_CM`、`SAFE_RIGHT_CM`；仅本次上电有效 |

例如 ESP32 客户端确定测试场地左侧允许 10 cm 最小净空时，向 Uno 发送 `CMD 21 SET_SAFE_DISTANCE LEFT 10`；收到 `ACK 21` 和 `DONE 21 SAFE_DISTANCE_SET` 后，可用 `CMD 22 GET_CONFIG` 核对。该命令配置的是安全阈值，不提供实时距离；左转时仍以 Uno 左侧超声波的有效读数作最终判断。

例如寻找猫时，上位机发送 `MOTOR -75 75` 并在 500 ms 内重复发送；上位机通过摄像头发现猫进入画面时发送 `STOP`。**不要求猫处于画面中央**。`SEARCH_CAT_START` / `SEARCH_CAT_STOP` 是上位机动作，不是 Uno 命令。缓慢接近、走停试探、快速直线逃跑、被抓左右轻摇等动作同样由上位机拆成上述底层命令。

## 本地安全规则

- 任一触碰开关出现新按下事件，当前运动立即中止。仍按着时，普通新运动被拒绝；只允许 `TURN` 的单次时长不超过 400 ms、PWM 不超过 90 的轻摇命令。若轻摇期间又有新的按下事件，同样中止。轻摇用定时转动，是因为每圈仅 20 脉冲无法精确分辨 9° 之类的小角度。
- 双轮前进或单轮向前划弧时，前向摄像头距离无效、超过 500 ms 未更新或小于 15 cm，拒绝或中止运动。左右超声波每侧交替触发；向左转检查左侧，向右转检查右侧，读数无效、超过 500 ms 未更新或小于该侧通过 `SET_SAFE_DISTANCE` 设置的最小净空时拒绝或中止转向。默认每侧 5 cm。设置阈值不会替代实际超声波读数，读数无效或过期仍拒绝动作。`GET_DISTANCE` 分别回报摄像头前向输入 `DISTANCE`、左侧超声波 `SIDE_LEFT`、右侧超声波 `SIDE_RIGHT`。超声波发射方向与安装方向必须一致。
- 对应编码器启用时，任一应转动的车轮超过 1200 ms 没有新脉冲，回 `ENCODER_STALL` 并停机。真实电机输出及编码器均关闭的纯虚拟轮子使用名义脉冲并保留停转检查；真实电机开启且编码器关闭时，仅使用定时动作，没有真实停转检测。计数型运动最多 10 秒；`MOTOR` 超过 500 ms 未续发则 `DEADMAN_TIMEOUT`。
- 测速光槽传感器不能测出实际方向，也不能检测轮胎在地面打滑；避障阈值和所有 PWM 都需实物标定。

## 无硬件调试接口

固件默认编入调试命令，但**上电时调试模式关闭，电机模式为 LIVE**。连接网页的“串口连接”模式后，先手动启用：

```text
CMD 1 DEBUG_MODE ON
CMD 2 DEBUG_MOTOR_MODE DRY_RUN
CMD 3 CAMERA_FRONT 100
CMD 4 DEBUG_INJECT SONAR LEFT 100
CMD 5 DEBUG_INJECT SONAR RIGHT 100
CMD 6 SESSION_START
CMD 7 DRIVE 9 95
CMD 8 GET_ENCODER
```

DRY_RUN 现在是测试初始化配置：允许模拟输入并关闭全部物理通道，之后用 DEBUG_IO <channel> ON|OFF 打开所接元件。DEBUG_OUTPUTS OFF 只关闭输出，保持输入选择；GET_IO 读取开关。真实超声波两项开启后拒绝模拟距离，真实触碰开启后拒绝模拟按压。真实电机开启时不产生虚拟编码器脉冲，未接对应编码器时 DRIVE/ROTATE 返回 PHYSICAL_ENCODER_REQUIRED；短时 MOTOR/MOVE/TURN 仍可测试，编码器关闭时没有真实停转检测。纯虚拟轮子保留名义脉冲及停转检查。前向及侧面输入有效期仍是 500 ms，模拟避障只供车轮悬空测试。

关闭调试不会自动开启输出；兼容命令 DEBUG_MOTOR_MODE LIVE 会恢复所有物理通道，测试期间请使用单通道开关。改变某轮电机/编码器来源会清零该轮计数。

```text
DEBUG_INJECT TOUCH TOP|LEFT|RIGHT|REAR PRESS|RELEASE|AUTO
CAMERA_FRONT CLEAR|BLOCKED|INVALID|0..400
DEBUG_INJECT SONAR LEFT|RIGHT 0..400|INVALID|AUTO
DEBUG_INJECT ENCODER <0..10000> <0..10000>
DEBUG_MOTOR_MODE DRY_RUN|LIVE
```

如不希望任何调试命令出现在生产固件，在编译选项中设 `UNO_ENABLE_DEBUG_COMMANDS=0` 后重新编译。串口没有身份认证；这一开关只提供构建层面的隔离。

V5.2 观测边界修正：删除空板默认 400 cm 侧面净空，并为模拟侧面测量添加 500 ms 有效期。网页每次运动前同步当前测量，运动期间持续刷新；协议调试命令仍供固件测试使用，但网页自主控制不允许操作者用原始命令覆盖测量。

## GET_TELEMETRY 回报格式

接收 CMD <id> GET_TELEMETRY 后 ACK，按顺序回报以下紧凑行，再 DONE <id> TELEMETRY。逐行发送只在超声波空闲且 TX 至少空出 60 字节时进行，无新增大缓冲；每行少于 60 字节。输入、状态和安全循环仍在每行之间执行。各行采样时间略有不同，并非同时锁存。

| 行类型（DATA <id> HW 后） | 位置字段 |
| --- | --- |
| STATE | 上电 ms、IDLE/MOVING/STOPPED、调试开关、测试输入开关、14 位 IO 掩码 |
| ML / MR | 电机输入指令、引脚驱动有符号值、PWM 幅值、输出开关、IN1/IN2 电平 |
| EL / ER | 编码器输入开关、原始电平、累计脉冲、RPM、来源 P/V/O |
| SL / SR | TRIG 开关、ECHO 开关、原始 ECHO 电平、物理距离、控制距离、来源 P/I/O |
| T | TOP/LEFT/RIGHT/REAR、输入开关、原始电平、去抖按压状态、来源 P/I/O |
| OUT | LED 开关、逻辑亮灭、引脚电平、蜂鸣器开关、逻辑响停、引脚电平 |
| FRONT | 前向控制距离、TEST/HOST 来源 |

P 为物理输入，V 为虚拟脉冲，I 为测试注入，O 为输入关闭；原始电平 -1 表示未采样，距离 -1.0 表示无效。IO 掩码按 IO_NAMES 顺序，第 0 位 MOTOR_LEFT，最后为 TOUCH_REAR。

## V5.6 低流量串口与丢帧恢复

主固件和网页统一 57600 波特，启动握手 READY UNO_CAT_V5.6。必须重新烧录主固件并刷新页面后连接；独立 SimpleMotorTest 仍为 115200，不适用于网页协议。

网页在线上使用 C id CF / GT / DI S L|R 等短指令，并加 *HH CRC-8（多项式 0x07）；缺少或校验失败的 C 帧不执行。手动 CMD 长指令仍可用。ACK、运动开始/完成/中止分别缩短为 AK / MS / MD / MA，均带 CRC；网页验证后还原成人可读日志。删除两侧注入测距的冗余 EVENT 回显，保留 ACK/DONE。未增加重复状态同步、运动状态轮询或无条件重发动作。硬件卡片每 250 ms 刷新，运动开始不额外触发整批刷新；运动脉冲另用短 MP 帧回报，不增加重复 STATE 同步。

修复：开始回报乱码时，可用已有 ACK 激活动画；若开始与 ACK 均丢失，最终完成回报仍能用对应待执行命令和起始位置校正位移，而不会不断重复同一路段。若所有有效运动回报都丢失，无法保证连续画面；不会把发送命令本身当作硬件已执行。关键运动帧有校验，其余遥测仍使用原协议，软件容错不能消除供电噪声。导出日志补充 IO 开关、最后的遥测读数及协议版本。

## V5.6 持续开关控制

前进和后退在串口模式下使用 MOTOR 开启、KEEP 每 200 ms 续期、STOP 关闭，由电脑根据 MP 累计脉冲决定停止；距离控制需要真实电机对应的真实编码器。续期不切断引脚，也不重置运动编号或脉冲基准。相同 MOTOR 命令同样只续期。通信中断超过 500 ms、电机堵转、障碍和触碰仍由 Uno 停止。电脑超过 750 ms 没有有效运动反馈，会停止距离动作。

巡游取消每 12 cm 必停的上限，持续接近标签，仍会因标签对齐、障碍或停靠而停止。默认提前停止余量 3 cm，可在几何参数区调整。硬件卡片正/反转按钮改为开/关，再按同方向按钮关闭该轮，可组合左右轮；总停止按钮关闭两轮。
