# 旋转避障与 DEV 合并评估（2026-10-13）

结论：可以移植，但不建议原样直接合并后部署。DEV 分支和已跟踪源码未修改；仅生成隔离评估副本，没有创建提交、推送、烧录或硬件动作。

## 当前版本和来源

- 已成功执行 git fetch origin --prune。
- DEV：4e6101c；origin/main：8fb1bbc；共同祖先：0d9167f。
- 远程独有提交：a3bb754（90 度检查点），b8423ee（替换为转前评估和连续旋转），8fb1bbc（Uno 侧向默认阈值 1 → 3 cm，以及旋转遇侧障后的转后检查）。
- 最终实现位于 esp32-host/ESP32CatHost/policy.cpp。该功能用于猫互动中的逃离转身，不是通用巡游绕障规划。

## 最终执行流程

ESCAPE_DIRECTION → STOP 确认 → ESCAPE_PRETURN_SETTLE → ESCAPE_PRETURN_OBSERVE → ESCAPE_TURN → STOP 确认 → ESCAPE_POSTTURN_SETTLE → ESCAPE_POSTTURN_OBSERVE → ESCAPE_DRIVE → ESCAPE_MOVE。

停稳需要新的编码器样本、零 RPM 和静止窗口；观察主动请求 FLOOR，侧向数据必须晚于观察请求且未过期。转前要求前方 CLEAR、左右读数有效，转向侧至少 15 cm，另一侧至少 10 cm。通过后连续执行计划转角（逃离主路径为 180 度）。侧距不足而前方 CLEAR 时最多前移 4 cm 两次并重新评估。转后前方与左右全部 CLEAR 才进入逃跑。旋转中收到 SIDE_* 中止时先停车并执行转后检查。

## 合并验证

git merge-tree --write-tree DEV origin/main 返回冲突，涉及 policy.cpp 和 项目日志.md。代码共两处冲突：视觉模式/遥测调度，STOP 确认处理。

candidate/ 是合并树的隔离源码副本，仅解决两处策略代码冲突：保留 DEV 的 policyInteractionStopping、APPROACH_CAT/PROBE_DRIVE 的 CAT 请求及 interactionStopPending 处理，同时加入逃离的 FLOOR 请求、250 ms 遥测及停稳初始化。日志冲突未解决，也未执行真正的 git merge。

聚焦 native C++ 验证结果：

- approach-front：144 checks passed。
- front-switch：8 checks passed。
- action-resume：41 checks passed。
- policy-core：编译成功，line 100 失败，旧断言期待 ESCAPE_TURN，实际进入 WAIT_STOP。origin/main 的同一断言也仍期待直接旋转。
- cat-interaction：编译成功，line 105 失败，同样是旧断言期待 ESCAPE_TURN、实际 WAIT_STOP。

以上属于离线测试，未编译完整 ESP/Uno 固件，不能证明实车旋转净空或制动安全。

## 移植时需要处理的问题

1. 转前检查中 front==CLEAR 且重试次数不足即可短移，没有要求侧距为已知状态：侧距 UNKNOWN 也会进入 ESCAPE_NUDGE_MOVE。这不是将 UNKNOWN 判为安全旋转，但仍会在侧距未知时发起前移。
2. ESCAPE_NUDGE_MOVE 的 policyVisionMode 返回 OFF，持续运动处理仅覆盖 CAT_DRIVE/PROBE_MOVE/ESCAPE_MOVE，短移缺少同等级的持续前向监测。
3. WAIT_TOUCH_RESUME 在旋转剩余角度小于 1 度时直接 awaitStop("ESCAPE_DRIVE")，可绕过新加入的转后检查；短移结束后触碰恢复的零剩余距离路径也落到 CHECK_PROBE，需要为逃离短移单独恢复到转前检查。
4. 新的转前/转后阶段没有纳入 escapeStage 的 DIRECTION 映射，界面可能显示 NONE。
5. 8fb1bbc 同时修改 Uno 默认侧向阈值、模拟器、测试及历史文档。移植时必须明确是否一并采用 3 cm，不应把它当作仅 ESP 策略变更。

建议：以最终远程方案为基础移植，修补上述安全门控及恢复路径，并更新受影响的测试；保留 DEV 最近的前向开关、互动停车及遥控逻辑。离线测试通过后，实车验证仍需覆盖左右朝向、旋转扫掠、地面视觉标定、触碰中断恢复和侧障中止后的行为。