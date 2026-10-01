# XIAO ESP32-S3 Sense 上位机

分支 `esp32-sense-host`，基线 `9217cc9`。关键阶段分别提交，保留现有 Uno 测试台。

## 连接

双击仓库根目录 `启动ESP32测试网页.bat`，自动启动本地服务器并打开 ESP32 页面；已运行则复用。端口被旧版本或其他服务占用时提示关闭对应服务，不自动结束进程。启动日志在忽略版本管理的 `esp32-host/vendor/web-8765*.log`。PowerShell 也可运行 `./esp32-host/start-web.ps1`；`-NoBrowser` 只启动服务，`-Port 8766` 可选择其他端口。它启动的是电脑测试网页；开发板调试网站随固件上电启动。

从原测试台打开“ESP32 实机上位机”，或访问 `http://localhost:8765/esp32-lab.html`。USB 使用 115200 波特，选择 COM11；网页模拟 Uno，ESP32 接收观测并运行决策。切换 UART 后不接受模拟输入。

开发板创建热点 `ESP32-CatHost`，密码 `catrobot32`。调试页 `http://192.168.4.1/`，HTTP MJPEG 流 `http://192.168.4.1:81/stream`。调试页可配置 2.4GHz Wi-Fi；热点保留，站点 IP 在状态中显示。

2026-10-01 接好外置天线后，完整固件已连接笔记本热点，当前地址 `192.168.137.22`；HTTP 页面、Uno 收信日志和视频解码已实测通过。IP 可能改变，以实时状态为准。电脑页面连接 USB 后会自动填写 stationIP 对应的视频地址和调试页链接。

也可连接笔记本移动热点：开启 2.4GHz 热点并接好开发板外置天线，通过 USB 或板载调试页保存热点信息。连接成功后使用状态里的 `stationIP` 访问 `http://<stationIP>/` 和 `http://<stationIP>:81/stream`；电脑测试页的视频地址也改成该流地址。固件重启会读取已保存配置自动连接。`http-smoke.py --host <stationIP>` 可验证此网络上的视频。

真实 Uno UART：ESP32 D6/GPIO43 TX 接 Uno RX，Uno TX 经 5V→3.3V 电平转换接 ESP32 D7/GPIO44 RX，共地；57600 波特。USB 控制和 UART 是独立通道。先断开电机输出验证协议，再装轮验收。

## 路线

`route` 是有序 AprilTag ID 数组，例如 `[7,42,2,100,7]`，按顺序循环。支持 1–128 个节点，可重复访问；tag36h11 ID 范围 0–586。停止策略后保存，存于 ESP32 NVS，重启回读；上电不自动运动。

## 版本与验证

- 通信阶段：USB JSON 行、UART CRC、HTTP 调试页、独立流服务器、网页 JavaScript Uno 模拟入口。网页 65 项测试通过；此阶段策略/视觉文件为下一阶段接口，尚不提供识别或自主巡游。
- 已接入板载 ESPDet-Pico 猫检测、完整 tag36h11 识别，以及本地巡游/互动状态机。默认仅提供约 5fps 预览，不运行推理；在网页选择“只识别标签”“只识别猫”或“两种识别”。每次检测后休息 200ms，两次检测启动至少间隔 350ms，上电重新回到仅预览。实际观测周期包含推理和队列耗时；此加频试验尚未完成板上稳定性和频率验证。
- 状态机支持按路线搜索/对准/接近标签、编码器 360° 搜索上限、200ms KEEP、750ms 无有效脉冲反馈停车、四区触碰、声光探索、捕猎逃跑、主动 HTTP 找猫、三段试探、装死等待及会话关闭/重开确认。距离停止使用 Uno 确认的几何参数；实体模式不能以模拟值补充缺失观测。
- 实机检查记录在 `VALIDATION.md`。首次烧录后发现控制任务栈溢出，已将两个 4KB 缓冲移出栈并增加任务栈。编译、启动、视频和识别质量分别记录，不把编译成功等同于实物验收通过。

### AprilTag 纠错内存评估（2026-10-01）

当前 `tag36h11` 使用 0 位纠错。目标 ESP32-S3 编译器确认 `quick_decode_entry` 为 16 字节；按本仓库 `quick_decode_init` 的容量公式，0/1/2 位纠错的码表分别为 28,176 / 1,042,512 / 36,544,272 字节。历史双识别日志中 PSRAM 最低剩余 6,606,424 字节，且固件已配置大块 malloc 使用 PSRAM。1 位纠错在容量上可行，2 位超过 8MiB PSRAM 总容量。此评估没有开启纠错；历史剩余总量不等于当前最大连续空闲块，开启前后仍需验证分配成功、峰值剩余和识别耗时。纠错仅容忍标签编码位读错，不能替代标签四边形检出。

## 前向标定与边界

地面外观检测路线来自先前的单目地面/障碍研究讨论，相关论文引用与当前简化范围见 [前向视觉研究依据](./FRONT_VISION_REFERENCES.md)。当前实现不包含论文中的 IPM、MRF 分割或距离估计；分块数量与阈值是项目参数。

前向默认 `INVALID`；视频可见不代表可前进。原型提供地面外观参考：停止策略，固定实车相机，用 15cm 标尺对应图像警戒区调整 `floorRoiTop`，确认当前警戒区是空地后点采集参考。中央一半视野、下部 ROI 分成 24 块，与参考灰度均值比较；接近的猫或外观变化返回 `BLOCKED`。这是定性外观检查，不是深度测距，同色障碍可能漏检，实车避障还需现场验证。相机高度、朝向、光照改变后重新标定。

只启用一种检测时，另一种视觉观测标为不可用。猫未检出、触碰松开都不证明离开；装死等待超时未确认则保持停车，现场确认后人工恢复。

## 指示灯

官方说明：未接电池时，USB 接入后红色充电灯约 30 秒会熄灭，这不表示断电或过热。来源 https://wiki.seeedstudio.com/xiao_esp32s3_getting_started/#battery-usage 。排查是否运行应看 COM11、启动日志、HTTP 状态和 uptime；本次没有实测板温或电源电压，不能据指示灯推断温度。

## 构建

PowerShell 运行 `./build.ps1` 编译；加 `-Upload -Port COM11` 烧录。构建目录使用 Windows 临时目录的英文路径，避免当前中文项目路径导致链接失败。`generate-config.mjs` 从原网页的概率和参数范围生成校验头；修改参数定义后再运行它。模型生成脚本由构建脚本自动执行并检查 SHA-256。

工具链：Arduino CLI、`esp32:esp32@3.3.12`、ArduinoJson 7.4.3。板型 `esp32:esp32:XIAO_ESP32S3:PSRAM=opi,PartitionScheme=max_app_8MB`。PSRAM 必须开启。

资料：[Seeed 开发板说明](https://wiki.seeedstudio.com/xiao_esp32s3_getting_started/)、[Espressif ESP-DL](https://github.com/espressif/esp-dl)、[AprilTag ESP32 库](https://github.com/raspiduino/apriltag-esp32)。识别库及模型来源和许可证另见 `THIRD_PARTY.md`。
