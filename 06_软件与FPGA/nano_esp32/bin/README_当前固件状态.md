# 当前固件状态

Nano ESP32 **0.2.2** 于2026-11-01编译通过。程序973429字节（30%），静态内存103276字节（31%）；应用BIN为973792字节，镜像校验及摘要均有效。精确输入、工具链与产物SHA256见[构建验证](../../../05_验证资料/Nano固件构建/构建验证.json)。

- 包含六个语音资源ID，新增`baseline_done`；WAV需另放J603卡`/voice/`。
- 新增`nfc off/on`，可不拆线停止/恢复NFC访问；`spi`显示瞬时SPI状态。重启恢复NFC默认开启。
- 保留J605 UART0/USB双控制台、AX UART1、APP网关及PFAIL保护。
- 开发板Arduino Nano ESP32 ABX00083，板包`2.0.18-20240930.arduino3`；FQBN `arduino:esp32:nano_nora:PartitionScheme=default,PinNumbers=default,USBMode=default`。
- J605固定115200、8N1、无流控、3.3V逻辑；1=GND、2=Nano TX、3=Nano RX。

当前构建尚未烧录，NFC接入导致SD/语音异常尚未实测定位或确认修复。基线提示的PS调用已在本地源码修改，但尚未部署；语音卡也尚未加入新WAV。此前0.2.1的USB运行验证仅属于旧版本。

`.ino.bin`是应用，`.bootloader.bin`是引导，`.partitions.bin`及`partitions.csv`是匹配分区，`.elf`用于调试。[SHA256SUMS.txt](SHA256SUMS.txt)列出校验值。按[烧录说明](../README.md)选择Nano ESP32并使用其DFU上传流程，分段BIN不能直接混写。
