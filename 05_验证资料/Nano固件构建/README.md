# Nano ESP32构建验证

当前固件 **0.2.2** 于2026-11-01使用已安装的Arduino Nano ESP32实际板包编译通过。构建输入与仓库、ASCII暂存副本逐一核对SHA256，ELF含0.2.2和baseline_done；应用镜像校验和及验证摘要有效。

- [构建记录](构建验证.json)：工具链、输入及产物SHA256、当前实测边界。
- [构建输出](build.log)：程序973429字节（30%），静态内存103276字节（31%）。
- [镜像检查](image_info.log)：Checksum和Validation Hash均valid。
- [主机测试](host_tests.log)：控制台10类场景（400条并发日志）、WAV解析、APP网关及新增NFC停用/恢复控制通过。
- [预编译文件](../../06_软件与FPGA/nano_esp32/bin/)与[固件说明](../../06_软件与FPGA/nano_esp32/README.md)。
- [贴装BOM核对](u601_smt_bom.json)：U601最终物料C548583、74LVC2G125DC-Q100H，与原设计针序和低有效使能一致。

NFC主机测试模拟SPI响应，覆盖初始化前禁用、停止失败重试、等待应答时中止、重新启用、片选与事务锁平衡，并检查baseline_done白名单；不模拟供电、布线、RF干扰或真实SD/NFC并发电气表现。语音包含六条PCM16LE/16kHz/单声道WAV；新基线提示3.557秒。

**0.2.2尚未烧录，实板故障未定位；新WAV未复制到J603卡，PS本地基线调用修改也未构建部署。** `upload.log`、`console.log`及JSON中的`previous_firmware_flash_verification`只记录此前0.2.1的USB烧录/运行结果，不属于当前版本。J605和本版载板外设仍待实测。
