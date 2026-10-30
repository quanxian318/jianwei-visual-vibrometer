# 当前固件状态

本目录为2026-10-30实际板包完整编译通过的Nano固件 **0.2.1**，增加J605 UART0与USB-C双控制台，保留APP网关及掉电功放保护。以提交`5cad5b8e2121a96ab0f65cc9045601560e1dfbbf`为基础修改；39个构建输入与ASCII暂存副本逐字节核对，精确内容见构建验证JSON中的源文件哈希。

- 开发板：Arduino Nano ESP32 ABX00083。
- 板包：Arduino ESP32 Boards `2.0.18-20240930.arduino3`。
- FQBN：`arduino:esp32:nano_nora:PartitionScheme=default,PinNumbers=default,USBMode=default`。
- Arduino IDE 1.8.19 builder；编译器 `xtensa-esp32s3-elf-gcc esp-2021r2-patch5-8.4.0`。
- 程序972037字节（30%）；静态内存103276字节（31%）。
- J605：115200、8N1、无流控；1=GND，2=Nano TX接适配器RX，3=Nano RX接适配器TX。使用3.3V逻辑，不接适配器供电脚。
- 输入`ping`应回复`pong`；`help`、`status`、`sd`、`nfc`等命令与USB-C共用，长回复分段发送。

`.bin`为应用，`.bootloader.bin`为引导文件，`.partitions.bin`及`partitions.csv`为匹配分区，`.elf`用于调试。各文件校验值见[SHA256SUMS.txt](SHA256SUMS.txt)。烧录优先按[固件README](../README.md)使用Arduino IDE；这些分段BIN不能直接合并或不带对应地址混写。

构建日志、39个输入文件和各产物哈希见[构建验证](../../../05_验证资料/Nano固件构建/构建验证.json)、[日志](../../../05_验证资料/Nano固件构建/build.log)。[主机测试](../../../05_验证资料/Nano固件构建/host_tests.log)覆盖双端口输入、64字节FIFO下的长回复、慢终端、队列边界和并发日志。

**0.2.1尚未烧录，J605及载板外设没有完成实物验证。** 0.2.0曾完成DFU上传与短USB命令响应验证；此前Nano的RESET约0.6V硬件异常需独立解决，不能以本次编译通过替代硬件修复。
