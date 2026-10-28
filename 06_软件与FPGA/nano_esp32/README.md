# Arduino Nano ESP32固件

使用Arduino Nano ESP32 ABX00083，板包 **Arduino ESP32 Boards（Arduino）**，FQBN为`arduino:esp32:nano_nora`。程序包含AX链路、PN532 SPI、辅助SD、I2S语音、Wi-Fi/NTP、PFAIL和HTTP手机网关。

- [主程序](vibmon_nano/vibmon_nano.ino) · [引脚定义](vibmon_nano/board_pins.h)。
- [30针硬件对照](../../04_使用指南/Nano逐针定义.md) · [SEL和PN532拨码](../../04_使用指南/模块上电前设置_SEL与NFC拨码.md)。
- [已完整编译的固件](bin/README_当前固件状态.md) · [构建验证](../../05_验证资料/Nano固件构建/构建验证.json)。
- [SD卡文件](sd_card/) · [五条语音清单](../../04_使用指南/语音文件清单.csv)。

## 编译与烧录

2026-10-28已用实际Nano板包完整编译通过，`bin/`与当前源码一致；烧录和整机联调尚未验收。

1. 主载板断电，取下Nano，单独用USB-C数据线连接电脑。
2. Arduino IDE中选 **Arduino Nano ESP32**，Pin Numbering选 **By Arduino pin (default)**，USB Mode选 **Normal mode (TinyUSB)**，分区保留 **With FAT partition（默认）**。验证板包版本为`2.0.18-20240930.arduino3`。
3. 将本仓库`06_软件与FPGA/shared/vibproto`文件夹复制到Arduino Sketchbook的`libraries/vibproto`，确保`library.properties`与`src`直接位于其中。无需另装PN532、音频或ArduinoJson库。
4. 打开`vibmon_nano/vibmon_nano.ino`，保留相邻全部.cpp/.h文件，选择Nano端口，验证并上传。没有运行端口时可双击RST进入启动加载模式后重新选择端口。该板使用DFU上传流程，不套用通用ESP32 Dev Module的擦除/引导文件教程。
5. 上传后打开USB串口监视器，115200、换行，发送`status`；应看到版本0.2.0。单独USB供电时SD/NFC/AX链路未连接、PFAIL状态异常均需结合载板连接判断。
6. 断开USB，对准D13/VIN、D12/TX丝印插回J604/J606；核对主载板电源后再上电。载板12V仅接Nano VIN，不能插偏到VBUS/3V3。

Windows已有Arduino IDE 1.8.19及板包时，在`06_软件与FPGA`执行：

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File nano_esp32\build_nano.ps1 -ArduinoDir D:\Arduino
```

脚本使用已安装工具链，把源码复制到ASCII临时路径构建，输出到`bin`；只编译，不烧录。默认临时目录`%TEMP%/vibmon_nano_build`会清空重建，不在其中存放其他文件。IDE遇到中文长路径问题时，可将仓库解压到较短的英文路径。

## 语音卡与模块设置

把`sd_card/voice`整个文件夹复制到J603所插FAT32卡根目录，形成`/voice/measure_done.wav`等五个路径。这是Nano辅助卡，与AX7010 Linux启动卡分开。

五条文件为选定的“清甜灵动”女声：百度度清影，per=4196，16kHz、16bit、单声道PCM WAV。测量完成使用已选试听；预警、报警和异常用同一声线的中性语气。文案、时长、合成参数和SHA256见[合成记录](sd_card/语音合成记录.json)。播放无需联网，也无需百度API密钥。五条文件已完成SD卡写入后的读回哈希校验，板端播放未测试。

PN532 V3走SPI：拨码1 OFF、2 ON，按本块开关ON标记设置并重新上电。J602-8为NC，不接模块RSTO。J601接无源喇叭，SPK_P/SPK_N两脚均不是GND。MP34DT01不经Nano，由AX的PL/PS处理。

## 通信与检查

UART默认115200、8N1、RTS/CTS，与PS源码默认配置一致；NVS历史设置可能覆盖默认值，更改baud后按提示重启。Nano/AX串口、SPI和I2S已在载板连接，不另加跳线。

默认热点`VIBMON-xxxx`，密码`vibmon2026`，地址`192.168.4.1`，HTTP 80、发现UDP 48300。测量状态需要AX/PS发布，热点出现不等于整机已经联通。

USB控制台可用：

```text
help
status
events
sd
nfc
play measure_done 20
stop
wifi <ssid> [password]
ap on
ap off
baud 115200
flow on
reboot
```

当前日志/控制台使用USB-C `Serial`；J605虽然连到D0/D1，但代码未初始化Serial0。完整接口和验证边界见[APP说明](../../04_使用指南/APP使用与开发.md)及[三侧审查](../../05_验证资料/三侧对接/三侧对接审查.md)。
