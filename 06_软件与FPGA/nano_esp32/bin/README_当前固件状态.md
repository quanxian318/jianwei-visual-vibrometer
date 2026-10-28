# 当前固件状态

本目录为2026-10-28完整编译通过的Nano固件0.2.0，含APP网关及当前掉电功放保护实现。构建输入与提交 `fe3e53a112bd2959b41dc268133cc13a5896066c` 中的37个Nano/共用协议源文件逐字节一致，未修改源码。

- 开发板：Arduino Nano ESP32 ABX00083。
- 板包：Arduino ESP32 Boards `2.0.18-20240930.arduino3`。
- FQBN：`arduino:esp32:nano_nora:PartitionScheme=default,PinNumbers=default,USBMode=default`。
- Arduino IDE 1.8.19 builder；编译器 `xtensa-esp32s3-elf-gcc esp-2021r2-patch5-8.4.0`。
- 程序971061字节，约30%；静态内存70460字节，约21%。

`.bin`为应用，`.bootloader.bin`为引导文件，`.partitions.bin`及`partitions.csv`为匹配分区，`.elf`用于调试。各文件校验值见[SHA256SUMS.txt](SHA256SUMS.txt)。烧录优先按[固件README](../README.md)使用Arduino IDE；这些分段BIN不能直接合并或不带对应地址混写。

构建日志、37个输入文件和各产物的哈希见[构建验证](../../../05_验证资料/Nano固件构建/构建验证.json)、[日志](../../../05_验证资料/Nano固件构建/build.log)。完整编译通过；尚未烧录或完成载板外设实测。
