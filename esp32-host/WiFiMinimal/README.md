# 最小 Wi-Fi 连线检查

只启用 Wi-Fi STA 和 USB 串口（115200），不启动摄像头、HTTP、识别或机器人控制。

复制 `secrets.example.h` 为 `secrets.local.h` 后填写热点信息；本机凭据文件被 Git 忽略。烧录 `WiFiMinimal.ino` 到 XIAO ESP32-S3。成功时串口每秒打印 `CONNECTED IP=... RSSI=...`；失败时打印状态及 Wi-Fi 断开原因码。

本程序会替换板上完整固件；恢复时执行父目录的 `build.ps1 -Upload -Port COM11`。完整固件源码和 NVS 配置仍保留。

2026-09-30 实机结果：最小固件编译、COM11 烧录及 Flash 校验通过（程序 846,090 bytes；静态 RAM 45,140 bytes）。随后监听 40 秒，未出现 `CONNECTED`，反复输出 `DISCONNECTED reason=201` 和 `WAIT status=6`，日志见 `wifi-result.log`。当前 SDK 的 `esp_wifi_types_generic.h` 定义 201 为 `WIFI_REASON_NO_AP_FOUND`（未找到接入点）；36 为 `WIFI_REASON_STA_LEAVING`。这次结果不能归因于摄像头或识别负载，也不证明密码错误。需核实热点的 2.4GHz 广播和外置天线。
