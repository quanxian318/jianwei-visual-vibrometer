# 软件与FPGA

## 三侧源码

| 部分 | 源码入口 | 作用与状态 |
|---|---|---|
| AX7010 PS Linux | [ax7010/linux/vibmon](ax7010/linux/vibmon/) | 相机采集、多点测振、PL双目/声学服务、触屏UI、记录和APP API；105项主机集成检查通过 |
| AX7010 PL | [ax7010/pl](ax7010/pl/) | 视频/HDMI、Census/SGM双目、PDM接收；完整PL位移链和载板外设集成仍待完成 |
| Nano ESP32 ABX00083 | [nano_esp32](nano_esp32/README.md) | NFC、辅助SD、I2S语音、Wi-Fi/NTP、掉电和APP网关；0.2.0完整编译通过，bin与语音卡文件已更新；烧录/实物联调待验收 |
| APP Android/iOS/Windows | [mobile_app](mobile_app/README.md) | 共用Web界面、设备发现、HTTP API、网关控制；[安装与接口说明](../04_使用指南/APP使用与开发.md) |
| 共用协议 | [shared/vibproto](shared/vibproto/) | 帧、CRC、JSON及0x60/0x62 APP扩展；[基础协议说明](../04_使用指南/PS_Nano基础协议.md) |
| Vivado重建基础 | [ax7010/vivado](ax7010/vivado/README.md) | 当前team_design脚本依赖的两个基础工程目录，保持相互路径 |
| PC仿真 | [pc_sim](pc_sim/README.md) | 电脑摄像头和算法/RTL参考 |

## 构建与联调

PS主机构建在Linux/WSL中进入`ax7010/linux/vibmon`执行`make -j4 test integration-test`。ARM交叉编译使用已安装的arm-linux-gnueabihf工具链，例如：

```sh
make BUILD=build_arm CXX=arm-linux-gnueabihf-g++ CC=arm-linux-gnueabihf-gcc AR=arm-linux-gnueabihf-ar CXXFLAGS='-O2 -mcpu=cortex-a9 -mfpu=neon -mfloat-abi=hard' CFLAGS='-O2 -mcpu=cortex-a9 -mfpu=neon -mfloat-abi=hard' LDFLAGS='-static -pthread' all
```

Nano构建见其目录README；板包`arduino:esp32:nano_nora`，默认Arduino引脚编号。APP构建脚本在`mobile_app/tools`；CI证据放在`05_验证资料/APP验证/构建验证`。

三侧接口主链对应，但仍有APP声学字段与连续设置两项已复现问题；[本轮审查](../05_验证资料/三侧对接/三侧对接审查.md)已区分源码、固件构建和实物集成缺口。APP直连AX使用HTTP 8080，ESP网关HTTP 80；UART默认两端115200、8N1、RTS/CTS开启。网关不传相机预览和完整文件，使用完整功能时直连AX局域网。

## 板端与算法事实

[当前冻结事实](../05_验证资料/板端冻结/冻结说明与当前事实.md)记录此前板端版本、哈希和真实限制。当前main包含其后的APP接口，Nano bin已由对应源码完整构建。AX冻结程序与当前APP接口仍存在部署版本差异，载板PL/串口尚需匹配集成；本次没有修改算法、通信实现、PS/PL/APP源码，也未重新部署板子。

MP34DT01接J706，由PL接收、PS抽取/分析；[声学方案](../03_设计说明/MP34DT01音频辅助测振与J706复用方案.md)与[SEL接法](../04_使用指南/模块上电前设置_SEL与NFC拨码.md)可直接查阅。20cm深度精度和正式双目标定尚未验收。VL53L1X的官方ULD仍需按原厂流程取得后放入`third_party/vl53l1x_uld`；无库时仅识别型号，不输出真实距离。

其他说明：[屏幕/触屏/编码器](docs/UI与触屏编码器说明_20261021.md)、[FPGA视觉处理](docs/FPGA视觉处理方案_20261021.md)。
