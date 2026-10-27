# Arduino Nano ESP32固件

使用ABX00083，板包`Arduino ESP32 Boards`（`arduino:esp32:nano_nora`），Pin Numbering为默认By Arduino pin。

- [主程序](vibmon_nano/vibmon_nano.ino)；[引脚定义](vibmon_nano/board_pins.h)。
- [30针硬件对照](../../04_使用指南/Nano逐针定义.md)；[SEL和PN532拨码](../../04_使用指南/模块上电前设置_SEL与NFC拨码.md)。
- [当前bin状态](bin/README_当前固件状态.md)：旧bin不包含全部当前源码和APP网关；需完成整包构建后再烧录。

从`06_软件与FPGA`目录执行：

```powershell
powershell -ExecutionPolicy Bypass -File nano_esp32uild_nano.ps1
```

脚本复用本地已安装的Arduino工具链，将源文件放入ASCII临时目录构建。使用Arduino IDE时，先将`shared/vibproto`作为库提供，再打开`vibmon_nano.ino`。

功能包括AX链路、PN532 SPI、辅助SD、I2S语音、Wi-Fi/NTP、PFAIL和HTTP手机网关。UART默认115200、8N1、RTS/CTS；NVS中的历史配置可能覆盖默认值。PN532拨码1 OFF、2 ON，按本块开关ON标记设置并重新上电。MP34DT01不经Nano，由AX的PL/PS处理。

USB控制台可用`status`、`events`、`sd`、`nfc`、`play <asset_id>`、`stop`、`wifi <ssid> [密码]`、`baud <速率>`、`flow on|off`、`reboot`。完整接口及验证边界见[APP说明](../../04_使用指南/APP使用与开发.md)及[三侧审查](../../05_验证资料/三侧对接/三侧对接审查.md)。
