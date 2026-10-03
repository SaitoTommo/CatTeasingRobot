# Uno ↔ ESP32 独立串口测试

这是替换 UnoCatController 的临时测试固件，不控制电机或传感器。ESP32 使用加入 `UART_BENCH` 回应的完整 ESP32CatHost 固件，不需要换成另一个简化固件。两边都是 9600、8N1。

## 上传与接线

上传 Uno 时先拔下 Uno D0/D1 的外接线，关闭占用 COM10 的串口工具。上传成功后恢复：

- ESP D6 / GPIO43 / TX → LV4 → HV4 → Uno D0 / RX。
- Uno D1 / TX → HV3 → LV3 → ESP D7 / GPIO44 / RX。
- LV 接稳定 3.3V，HV 接稳定 5V，所有 GND 共地。
- 不保留 HV3–HV4 的回环短接线，不把 Uno 的 5V TX 直接接 ESP。
- ESP 网页选择 Real UART，并停止决策及其他测试操作。

## 自动测试（优先）

Uno 每两秒输出 `EVENT UART_BENCH_REQ HELLO_1`、`HELLO_2` 等。ESP USB 日志 / 网页日志应依次出现：

```
UART_BENCH_RX   EVENT UART_BENCH_REQ HELLO_1
UART_BENCH_TX   EVENT UART_BENCH_REPLY HELLO_1
UART_BENCH_ACK  EVENT UART_BENCH_RECEIVED HELLO_1
```

RX 证明 Uno→ESP 的请求被收到；TX 只证明 ESP 执行发送；ACK 证明 Uno 收到回应并成功回传确认，才完成双向测试。收到回应时 Uno 的板载 L 灯翻转。重复的 `READY UNO_UART_BENCH_1` 是程序重新启动的标记，打开串口监视器本身也可能通过 DTR 复位 Uno，不能把该次启动误判为电源故障。

自动测试无需电脑向 Uno 输入文字。为排除 Uno USB 串口桥对 D0 的影响，可以上传后改用稳定的外部 USB 电源，同时保持 ESP 的 USB 接电脑观察日志。

## 手动输入

Uno 串口监视器选 9600，行结束符选换行。输入 `AUTO OFF` 暂停自动请求，再输入 `hello`，Uno 发送 `EVENT UART_BENCH_REQ hello`，ESP 返回相应的 `EVENT UART_BENCH_REPLY hello`。输入 `AUTO ON` 恢复自动请求。

Uno 的 USB 串口和 D0/D1 共用同一个硬件 UART。电脑的 USB 串口桥 TX 与 ESP TX 都接到 Uno D0 时存在驱动冲突的可能；手动输入失败不能直接证明固件错误。无需再占用其他 IO，但它不是两个独立串口。为了避免无限回声，测试协议不会把 ESP 回应、EVENT 行或完整控制固件的 C 命令重新当作输入回传。

文本只支持可打印 ASCII，每行最多 159 字节，超长行整行丢弃。ESP 只在 Real UART、决策 IDLE/STOPPED、无其他调试操作时回应；测试消息不会计入真实控制器在线状态，也不会进入运动决策。

## 恢复

完成测试后重新烧录 `firmware/UnoCatController`，恢复原机器人功能。测试固件期间不要启动网页寻路或电机测试。仅编译成功不代表接线、供电或双向通信已通过。

当前为 9600 波特率对照实验。原 UnoCatController 仍为 57600，UnoUartLoopback 也仍为 57600；恢复机器人控制固件时必须同时将 ESP 的 uno.begin 与启动日志恢复为 57600。ESP USB 调试串口保持 115200，和 GPIO UART 的 9600 相互独立。现有 uart-probe.py 的 COM10 读取参数仍为 57600，不能用它的 USB 观察模式验证本次 9600 实验。
