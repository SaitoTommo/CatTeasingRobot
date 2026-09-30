# 板载视觉依赖

1. AprilTag ESP32 adapter 3.4.0：`raspiduino/apriltag-esp32` commit `5413a2926e26922f44ca12d49d58d84b22b9002d`，来源 https://github.com/raspiduino/apriltag-esp32 。源码纳入 `ESP32CatHost/src`，文件内保留 BSD/MIT 版权。`common` 中的 `#include "common/..."` 改成相对于当前文件的路径，适应 Arduino sketch 编译。
2. 完整 tag36h11 编号表：来源 https://github.com/AprilRobotics/apriltag/blob/b7c0ebe9aa20f82ec7a828579004f9e706bfecd9/tag36h11.c ，替换 adapter 原有的 35-ID 裁剪表，支持完整 587 个编号，BSD 版权仍在文件内。检测设置 hamming correction=0，避免完整字典造成额外内存开销；这意味着不修复标签误码。
3. 猫检测：Espressif ESP-DL ESPDet-Pico 224×224 int8，文件来源 https://github.com/espressif/esp-dl/blob/6d3c4da1087d35916858dd0f9b3e9d6a85186e11/models/cat_detect/models/s3/espdet_pico_224_224_cat.espdl 。模型 498576 bytes，SHA-256 `604501220b26b8a59f44a835b7d37c31444444a8f5661c5ad9469c61fbcd71e1`。Apache 2.0 许可证见 `ESP-DL-LICENSE`。模型初始化、letterbox 和后处理参数参照同仓库 `models/cat_detect/cat_detect.cpp`；推理库使用 Arduino ESP32 3.3.12 所带 ESP-DL。

`generate-model.mjs` 校验二进制后生成对齐的 C++ 头文件。原二进制纳入 Git，生成头文件忽略；因此模型更新必须同时修改校验值并记录来源。库源文件已在项目中，编译不依赖联网下载。ArduinoJson 7.4.3 由现有 Arduino 环境提供。
