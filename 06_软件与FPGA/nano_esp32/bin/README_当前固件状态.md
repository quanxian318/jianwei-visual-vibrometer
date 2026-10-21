# 当前固件状态

本目录现有 bin/elf 文件保留自本轮代码修正之前，不包含当前源码中的掉电功放保护修正。

当前 `power_service.cpp` 和 `audio_service.cpp` 已用 Nano ESP32 实际板包参数交叉编译成 Xtensa 目标文件，无警告。本轮没有完成整包重新链接或烧录。要使用当前源码，请先在 `06_软件与FPGA` 运行 `powershell -ExecutionPolicy Bypass -File nano_esp32\build_nano.ps1`，确认整包构建成功后再上传对应新固件。
