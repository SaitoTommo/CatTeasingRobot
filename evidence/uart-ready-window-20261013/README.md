# 接收窗口协议部署与静止验收（2026-10-13）

源码提交：db1d28d；回滚基线：276308ba693e5861b320b3e07de67d96aa9b2ebf。
按用户要求未做设备固件备份；回滚需按基线重编译两端，并将 Uno 接收线移回 D12。

用户确认接收线已移到 D13，烧录时断开 D1，烧录后恢复 D1。
Uno V6.14 / COM10：Arduino CLI upload --verify 成功退出 0；
USB 启动回报确认版本和 D13，65000 配置及 65001 完整 TB 遥测均收到。
ESP 0.50 / COM11：MAC 7c:4f:ad:1e:87:c0，先核对现有分区表一致，
仅写 0x10000 应用，esptool hash 校验通过；HTTP 回读确认新版本。
以上是上传校验和运行回读，不是完整固件逐字节另行读回比较。

离线：48 项页面/模拟测试通过；提取实际源码的窗口门禁、STOP/队列、
运动安全及 CRC 门禁四组检查通过。Uno Flash 32234/32256，静态 RAM 1376/2048；
ESP 程序 3840822 字节。固定一位小数输出及复用整数解析器用于控制容量。

## 一次 30 秒真实 UART 静止采集

采集总时长 30516 ms，MODE UART 发生在 ESP ms=130529。
统计仅包含该 MODE 之后的事件，排除导出开头的旧 SIM 超时和来源拒绝。

| 项目 | 实测 |
| --- | --- |
| RX available | 376 条 |
| GET_TELEMETRY | 15 次，15 次完整 AK / 三个 TB / DONE |
| 请求发送至完成 | 最小 38 ms，中位数 56 ms，P95/最大 70 ms |
| 遥测超时 / CRC 错误 | 0 / 0 |
| 初始窗口拒绝 | 1 次，SESSION_STOP，ERROR 0 RX_WINDOW_EXPIRED |
| 最后一组 13 项回报年龄 | 299–302 ms |
| 停车 | STOP_CONFIRMED，stopPendingId=0，stopUnconfirmed=false |

初始被拒绝的 SESSION_STOP 另做一次幂等安全重发：
UART_WINDOW_TX 为 R 3673 C 135 SESSION_STOP；14 ms 后收到 DONE 135 SESSION_STOPPED。
最终 STATE 为 STOPPED，队列为空。没有发送运动指令。
右侧超声波仍为 -1 无效，前向 Uno 行为 HOST/-1；不能把完整传输等同于所有传感器有效。

旧用户日志是 47 次请求中仅捕到 2 次完整回报。这次同时改变了接收脚和协议，
且只是静止短窗口，不能据此分别确定原故障根因、长期稳定性或运动时可靠性。

原始事件和单项回报：uart-stationary-capture.json、uart-final-readback.json。
summary.json 为汇总；build-hashes.json 记录实际构建文件 SHA256。
