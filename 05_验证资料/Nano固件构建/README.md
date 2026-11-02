# Nano ESP32构建验证

当前 **0.2.3** 已于2026-11-02使用实际Nano板包编译通过。变更仅为语音资源表扩展到十三条及版本号；不修改采集或自动播报流程。输入与构建记录逐个校验SHA256，ELF含全部十三个ID，镜像校验及摘要有效。

- [构建输出](build.log)：Sketch uses 973865 bytes (30%) of program storage space. Maximum is 3145728 bytes.；Global variables use 103276 bytes (31%) of dynamic memory, leaving 224404 bytes for local variables. Maximum is 327680 bytes.
- [完整记录](构建验证.json)：输入及产物哈希、十三条WAV哈希、实测边界。
- [镜像检查](image_info.log)；[主机检查](host_tests.log)。NFC控制回归按当前资源表重新编译通过；未改动的控制台、WAV解析及网关回归复用已有结果。

本版尚未烧录、未听音验收。`upload.log`、`console.log`、`nfc_control.log`和JSON的`previous_firmware_flash_verification`属于此前0.2.2在Nano 7C0C5F54D5EC上的USB验证，不属于0.2.3。原五条WAV曾卡上读回；基线提示和新增七条尚未写入J603卡。PS基线调用修改未部署，七条新增自动触发未接入；NFC/SD实板故障仍未定位。
