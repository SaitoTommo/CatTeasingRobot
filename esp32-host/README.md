# XIAO ESP32-S3 Sense 上位机

分支 `esp32-sense-host`，基线 `9217cc9`。关键阶段分别提交，保留现有 Uno 测试台。

## 连接

从原测试台打开“ESP32 实机上位机”，或访问 `http://localhost:8765/esp32-lab.html`。USB 使用 115200 波特，选择 COM11；网页模拟 Uno，ESP32 接收观测并运行决策。切换 UART 后不接受模拟输入。

开发板创建热点 `ESP32-CatHost`，密码 `catrobot32`。调试页 `http://192.168.4.1/`，HTTP MJPEG 流 `http://192.168.4.1:81/stream`。调试页可配置 2.4GHz Wi-Fi；热点保留，站点 IP 在状态中显示。

真实 Uno UART：ESP32 D6/GPIO43 TX 接 Uno RX，Uno TX 经 5V→3.3V 电平转换接 ESP32 D7/GPIO44 RX，共地；57600 波特。USB 控制和 UART 是独立通道。先断开电机输出验证协议，再装轮验收。

## 路线

`route` 是有序 AprilTag ID 数组，例如 `[7,42,2,100,7]`，按顺序循环。支持 1–128 个节点，可重复访问；tag36h11 ID 范围 0–586。停止策略后保存，存于 ESP32 NVS，重启回读；上电不自动运动。

## 版本与验证

- 通信阶段：USB JSON 行、UART CRC、HTTP 调试页、独立流服务器、网页 JavaScript Uno 模拟入口。网页 65 项测试通过；此阶段策略/视觉文件为下一阶段接口，尚不提供识别或自主巡游。
- 后续阶段的实测结果在 `VALIDATION.md` 记录，不把编译成功等同于真实识别或装轮运行通过。

工具链：Arduino CLI、`esp32:esp32@3.3.12`、ArduinoJson 7.4.3。板型 `esp32:esp32:XIAO_ESP32S3:PSRAM=opi,PartitionScheme=max_app_8MB`。PSRAM 必须开启。

资料：[Seeed 开发板说明](https://wiki.seeedstudio.com/xiao_esp32s3_getting_started/)、[Espressif ESP-DL](https://github.com/espressif/esp-dl)、[AprilTag ESP32 库](https://github.com/raspiduino/apriltag-esp32)。识别库及模型来源和许可证另见 `THIRD_PARTY.md`。
