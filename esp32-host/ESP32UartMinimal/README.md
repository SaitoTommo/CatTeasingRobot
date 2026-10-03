# 独立最小 UART PING/PONG 测试

配套 Uno 固件为 `firmware/UnoUartMinimal`。两块板子的 GPIO UART 都是 9600 / 8N1，无 Wi-Fi、摄像头、网页、CRC 或机器人决策，不控制电机。ESP USB 调试串口仍为 115200；Uno USB 串口监视器为 9600。

ESP 每四秒发送 `PING ESP`，Uno 收到后回复 `PONG UNO`。Uno 也独立每四秒发送 `PING UNO`，ESP 收到后回复 `PONG ESP`。启动后的首次发送时间分别为一秒和三秒；分别复位可能改变相位，两边不依赖严格轮流才能工作。每次发 PING 后等待回应 1500ms，超时输出 PONG_TIMEOUT。保留双方名称，防止自身回环被误判成来自对方的 PONG。

ESP 成功收到 `PONG UNO` 时输出 `ROUND_TRIP_OK count=...`；Uno 成功收到 `PONG ESP` 时输出 `DBG UNO ROUND_TRIP_OK count=...`，并翻转板载 L 灯。Uno 的 DBG 输出也会到 ESP，可从 ESP USB 同时看到 Uno 接收字节日志，无需 Uno USB 接电脑。

完整的非空行接收后输出 RXHEX。十六进制记录的是除 CR/LF 外的原始接收字节；无换行的部分数据暂不输出，因此看不到 RXHEX 不能直接等同于完全没收到字节。最长保留63字节，超长行标记 LINE_OVERFLOW。意外文字不回应，DBG 行不回传，避免无限回声。

上传 Uno 前断开 D0/D1 外接线，关闭占用 COM10 的监视器。上传后恢复：D6→LV4→HV4→Uno D0/RX；Uno D1/TX→HV3→LV3→D7。LV参考接3.3V，HV参考接5V，共地，不保留回环短接线。不要把 Uno 的5V TX直接接ESP。

这次 ESP 简易固件替换掉网页固件，因此测试期间原 HTTP 网页和 API 不可用；在 COM11 的 USB 日志查看结果。编译/上传成功不等于真实接线通过。结束后可重新上传 ESP32CatHost 和 UnoCatController；恢复控制固件前须统一双方波特率，原 UnoCatController 为57600，而本次 ESP32CatHost 对照实验源码暂为9600。
