# D12 软件接收最小测试

9600/8N1。ESP 保持 `ESP32UartMinimal`。接 ESP D6→LV4→HV4→Uno D12；回程 Uno D1→HV3→LV3→ESP D7。Uno D0 不接 ESP。LV参考3.3V、HV参考5V、共地，移除所有回环跳线。D12 原蜂鸣器线必须移除。

SoftwareSerial 使用 D12 接收，构造函数的 TX 参数为已有 D1，但不会调用其 write/print 方法；所有发送仍由硬件 Serial 完成。USB监视器9600，ESP USB监视器115200。

2026-10-02 最小实机采样确认：两块板子均有自己产生的 ROUND_TRIP_OK，未通过 USB 注入测试请求。本次30秒记录中，Uno自身成功编号2–9，ESP自身成功编号2–8，没有PONG超时。COM11也转发Uno的DBG日志，不能把两种成功日志相加当作ESP成功次数。原始证据为 `esp32-host/minimal-uart-soft-d12.json`。

这证明最小固件的该通路可以双向通信；不证明完整固件下编码器、超声波和电机并行运行已验证。
