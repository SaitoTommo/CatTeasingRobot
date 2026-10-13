# 逃跑转后读取持续前向结果（2026-10-13）

用户运行记录：D:/data/download/esp32-local-history-2026-10-13T09-25-35-521Z.json，bootId=72fb133675354095，固件 0.48、UART、frontAvoidanceEnabled=false。

三次转向均发送 ROTATE -90.0 120，并收到 MD 完成回报。进入 ESCAPE_FRONT_CHECK 的板上时间分别为 165306 / 220854 / 256450 ms，退出时间为 168312 / 223854 / 259489 ms，均报 escape front vision timeout，未发出逃跑 DRIVE。这不能归因于前向 BLOCKED、电机不能直跑或界面开关没有保存。

原因：0.48 在停稳后调用 requestVision(..., "FLOOR")；实际 VisionGate::request 仅支持 CAT/TAGS，拒绝 FLOOR。政策测试的请求桩只记录请求，没有使用真实 VisionGate，未覆盖此集成缺陷。

0.49 改为与行驶阶段一样读取持续 FLOOR 结果。停稳后取消旧请求，记录观察开始时刻和旧帧号，进入 ESCAPE_FRONT_CHECK。仅接受 mode=FLOOR、新帧号且采集时间严格晚于观察起点的结果；不依赖一次性 ticket、generation 或 step。真实摄像头入口仍按视觉模式和 epoch 拒绝旧任务结果。CLEAR 允许直跑，BLOCKED 继续侧向搜索，无效/过期/未标定退出，无新画面 3 秒超时退出。旋转许可仍只看侧距。

普通前向开关对逃跑的语义保持原约定：逃跑前向保护一直开启。本次没有将关闭开关改成无视觉直跑。

测试请求桩已接入真实 VisionGate。新增/调整用例覆盖：无需 FLOOR ticket；旧帧、转后观察起点之前的画面、CAT/TAGS 画面不能发 DRIVE；连续 FLOOR 可以在 generation/step 不匹配时授权直跑；BLOCKED 仍可以继续下一次搜索；未知输入和超时停止。编译和测试结果见 validation.json。

源码基线 42c456a，回滚标签 rollback/pre-escape-continuous-front-20261013。固件版本 ESP32_CAT_HOST_0.49_ESCAPE_CONTINUOUS_FRONT。2026-10-13 用户授权后已在 COM11 烧录：核对 3072 字节分区表一致，仅写 0x10000 应用，按用户要求以重启后的版本号回读验收，没有额外应用哈希或完整应用读回。保留已知 0.48 应用文件用于回滚，此文件不是新的设备读出备份。

回读版本正确，IDLE / SIM_UNO、cameraOk=true、无活动电机或待确认停车命令。烧录前为 UART，重启后按现有启动默认回到 SIM_UNO；未自动切回 UART 或发出运动命令。烧录前后 config 完整 JSON 对比一致，包括 frontAvoidanceEnabled=false、escapeTurnClearanceCm=15、路线 [43,46,51]。证据见 esp-preflash-state.json / esp-after-state.json。台架运动验收仍待用户执行，需切到 UART 并核对转后新 FLOOR 的 ESCAPE_FRONT_GATE 日志及 DRIVE。
