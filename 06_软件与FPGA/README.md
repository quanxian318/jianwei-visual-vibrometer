# 06_软件与FPGA

按 09_团队开发资料_20261020 的引脚表、协议 V1 和开发说明编写。三部分共用 `shared/vibproto`（协议 V1 帧编解码、CRC、严格 JSON，C++11）。

当前开发的职责边界见 [PL、PS 与 ESP32 分工](../00_当前设计方案/PL_PS_ESP32职责与接口分工.md)：PL 的目标核心为实时 ROI 位移提取，PS 负责测量状态、背景补偿、标定/FFT、UI 和主记录，ESP32 负责 NFC、辅助 SD、语音与 Wi-Fi/NTP。本轮只明确分工，没有实现新的位移硬件或改变现有接线。

当前代码逻辑、审查结论与待实现项见 [代码审查与状态机说明](docs/代码审查与状态机说明_20261021.html)（[Markdown](docs/代码审查与状态机说明_20261021.md)）。测振有两种方式（`measurement.method`）：`roi` 为 PS 单框软件基线（NCC 或 LK）；`multipoint` 为队友的多点测振核（`ax7010/linux/vibmon/third_party/axvibration`，AN5642 板卡配置默认），测点框内网格测点加背景参考点，与停机基线比较后给出主频和各点像素 RMS；显示用的视觉处理链（金字塔降采样、时间带通、欧拉运动放大、振动能量）有 PS 实现和逐位一致的 PL RTL，PL 尚未接入厂家视频工程，见 [FPGA 视觉处理方案](docs/FPGA视觉处理方案_20261021.md)。

[屏幕、触屏和编码器操作说明](docs/UI与触屏编码器说明_20261021.md)；电脑摄像头仿真见 [pc_sim](pc_sim/README.md)。

最新 LED2 互锁修正与用户视频结果见 [视频验证报告](../12_视频验证_20261021/README.md)。测振算法推进（逆组合 LK 位移核、PL `lk_accum` 累加核 RTL、背景参考补偿、谱峰置信度、全视场谱线）见 [测振算法推进](../13_测振算法推进_20261021/README.md)。双目深度改进（PL 双目核 Census＋SGM＋左右校验、标定校正、PS 后处理与填补、真值评测）见 [双目深度改进](../14_双目深度改进_20261021/README.md)。

声学辅助：[MP34DT01 音频辅助测振](../00_当前设计方案/MP34DT01音频辅助测振与J706复用方案.md)。J706 原 I2C2 两脚改作 PDM：PL 接收核 `pdm_mic` 输出 2 MHz 时钟、按有效半周期采样并缓存原始码流与计时，PS 抽取为 31,250 S/s、求包络谱并与视觉主频对照，结果写入记录（`acoustic` 段、`acoustic.wav`、`acoustic_spectrum.csv`）并在界面显示；未上板。

## 目录与验证状态

| 路径 | 内容 | 已验证 | 未验证 |
|---|---|---|---|
| shared/vibproto | 协议 V1 编解码 | 资料包 4 个测试向量逐字节一致；Python 参考实现生成的 300 个随机帧编码/解码/再序列化一致 | — |
| nano_esp32/vibmon_nano | Nano ESP32 固件：AX 链路与会话、事件队列/IRQ、PN532、SD、I2S 语音、Wi-Fi/NTP、掉电、USB 调试命令 | 本轮修改的 power_service/audio_service 已用真实 Xtensa 板包参数编译无警告；WAV 解析主机测试通过 | 修改后整包重新链接、上板；bin 保留旧固件 |
| ax7010/pl | carrier_ctrl RTL、Vivado 集成脚本（载板设计用 pdm_mic 取代原 J706 的 I2C2）、约束、自检测试平台 | WSL Icarus 行为仿真通过：AXI 分离握手/背压/WSTRB、PWM、按键、事件 FIFO、WS2812、看门狗、PFAIL 停麦克风时钟 | Vivado 综合、实现、上板 |
| ax7010/pl/audio | MP34DT01 PDM 接收核 `pdm_mic`：2 MHz 时钟、有效半周期采样、32 位字 8192 深 FIFO（131 ms）、溢出冻结与丢字计数、读 WORDS 锁存字序号与时钟戳，AXI-Lite 0x44030000、无中断；`bd/team_add_pdm.tcl` 加入队友 AN5642 双目工程（`bd/team_design.tcl` 依次执行双目、PDM 和构建三步），载板设计由 `bd/add_carrier.tcl` 接入并用 PFAIL 硬件停钟 | Icarus 测试平台按 PS 的轮询方式读 1200 字 Σ-Δ 码流逐字一致，锁存覆盖 PDM 周期内全部 50 个时钟相位，含溢出冻结与缺口、清空、kill、两种相位（`sh pl/audio/sim/run_tests.sh`）；Vivado 2025.2 在队友工程中实现：接收核 251 LUT、352 FF、8 BRAM36，全设计时序满足（见双目一行） | 上板（线束波形、SAMPLE_AT 余量、连续采集）；载板设计尚未在 Vivado 中运行 |
| ax7010/pl/displacement | 位移引擎 `lk_accum`：双线性插值＋5 路 48 位乘加，整数 ZNCC 搜索与 LK 亚像素更新共用；自检测试平台与黄金向量 | 与 C++ 位精确模型 `lkAccumulate()` 逐位一致（合成纹理 8 例＋用户视频帧 8 例，Icarus 12，`sh pl/displacement/sim/run_tests.sh`） | 搜索序列器、窗口装载（roi_capture）、AXI 封装、综合、上板 |
| ax7010/pl/stereo | 双目核 `stereo_core` = `census_cs97`（9×7 中心对称 Census）＋`sgm_core`（Hamming 代价、2 路径 SGM、唯一性、亚像素、左右校验）；块设计封装 `stereo_axi`（AXI-Lite＋AXI-Stream＋中断），`bd/add_stereo.tcl` 接 AXI DMA/HP2 并生成设备树；`bd/team_bm_to_stereo.tcl` 把队友 AN5642 工程里闲置的 BM 换成本核＋AXI DMA（HP1，DMA 存储侧 64 位，本核、DMA 与 HP1 同为 FCLK2 40 MHz，vibmond 经 /dev/mem 访问，位流用 `tools/bit_to_bin.py` 转换）；测试平台与黄金向量 | 与 C++ `sgmRaw()` 逐位一致（合成台阶 96×40、Cones 裁剪 112×48；Census 入、灰度入、AXI 封装三级，后者含随机流停顿与两帧，Icarus 12，`sh pl/stereo/sim/run_tests.sh`）；Vivado 2025.2 在队友工程中与麦克风接收核一起实现：13.5k LUT（76.7%）、slice 98.6%、41.5 BRAM36，全设计 WNS +0.462 ns、WHS +0.007 ns（40 MHz 域最长路径 22.8 ns），位流 `an5642_stereo_sgm40_pdm.bit.bin` 与报告在 `pl/stereo/bitstream` | 上板、AN5642 实拍标定；100 MHz 需给 SGM 扫描加流水 |
| ax7010/pl/vision | 视觉处理 RTL：`vp_reduce` 流式降采样、`vp_core` 时间带通/运动放大/振动能量；测试平台 | 与 Python 模型逐位一致（合成帧和电脑摄像头实拍帧，含奇数高度、随机输入间隔） | AXI 接口封装、接入厂家工程、综合、上板 |
| pc_sim | 电脑摄像头实验台、位精确模型、Python/C++/RTL 比对脚本 | 摄像头实拍和合成数据下三方逐位一致；实验台自检运行 | — |
| ax7010/linux/vibmon | AX7010 Linux 程序 vibmond（六页触摸界面、PS 视觉处理线程、运行时设置、AN5642 相机与 PL 视频输出）、命令行 vibmonctl、联调工具 carrier_test、plcam、视觉参考工具 vision_ref、声学离线分析 wav_acoustic | WSL x86-64（g++ 13）编译无警告；11 组单元测试（含界面各页渲染与点按、视觉处理、LK 位移核、双目匹配与深度服务、PL 双目驱动对仿真硬件及寄存器表对 RTL 的核对、多点测振封装与原整帧算法逐帧逐位一致及基线/7 Hz 合成振动、PDM 驱动对仿真接收核及寄存器表对 RTL、抽取逐位一致、声学采集服务与分析）、64 项应用集成检查（含设置保存与重启生效、测点框尺寸、多点基线→测量→记录、合成音源的声学参考→记录与 WAV）、端到端冒烟测试；ARM 静态交叉编译无警告 | 上板：多点每帧耗时与 45 fps 是否丢帧、PL 双目核、麦克风连续采集与抽取耗时 |
| ax7010/linux/petalinux | 设备树片段、内核配置、Yocto 配方、开机自启 | 源码包可独立编译 | 在 PetaLinux 工程中构建 |

## Nano ESP32 固件

板包选 Arduino ESP32 Boards（arduino:esp32:nano_nora），Pin Numbering 用默认的 By Arduino pin。命令行构建：

```powershell
powershell -ExecutionPolicy Bypass -File nano_esp32\build_nano.ps1
```

**当前 bin 是本轮修改前的固件；本轮只完成两个修改源文件的目标编译，使用新保护逻辑须先运行完整构建。**

脚本把源码复制到 ASCII 临时目录，并用临时目录联接缩短板包路径（否则 ESP32 工具链的编译命令超过 Windows 32K 上限），不修改已安装的板包；产物在 `nano_esp32/bin`。用 Arduino IDE 打开时，先把 `shared/vibproto` 复制到 文档\Arduino\libraries，再按常规方式上传。

USB 串口命令：`status`、`events`、`sd`、`nfc`、`play <asset_id> [音量]`、`stop`、`wifi <ssid> [密码]`、`wifi off`、`baud <速率>`、`flow on|off`、`log <级别>`、`reboot`。波特率和流控保存在 NVS，重启生效，两端必须一致。AX 侧 UART16550 用 100 MHz 时钟时 921600 实际为 892857（误差 3.1%），高速档建议两端都用 781250（分频整除）。

协议实现的补充约定：Nano 启动产生 BOOT 事件；会话建立后才主动推送事件；HELLO 总是开启新会话并停止旧会话的语音；不在缓存窗口内且不大于已用 seq 的请求返回 SEQ_CONFLICT；HELLO 响应的 event_cursor 为 {last_event_id, acked_through, pending}；AUDIO_DONE 的 result 为 completed/stopped，异常结束用 AUDIO_FAILED；SET_NET 的 ssid 为空表示关闭 Wi-Fi。

## AX7010 PL：carrier_ctrl

集成步骤（Vivado 不能用中文路径，先把 `ax7010/pl` 复制到 ASCII 目录）：

1. 打开与板卡修订匹配、已能跑 AN5642/HDMI 的厂家工程和块设计。
2. Tcl 控制台：`source <路径>/bd/add_carrier.tcl`，再 `carrier::add`。脚本加入 carrier_ctrl 与 pdm_mic（模块引用）、2×AXI IIC、2×AXI UART16550、1×AXI Quad SPI，按 XDC 端口名建外部端口，把 6 个中断追加到 IRQ_F2P，固定 AXI 地址（0x4400_0000 起），并写出 `bd/generated/carrier_map.json` 和 `carrier_generated.dtsi`。
3. 工程使用自动生成的顶层时重新 make_wrapper；手写顶层需要手工加入 34 个端口。Vivado 不支持模块引用 inout 端口时，把 IOBUF 移到顶层。
4. 综合实现，检查时序、I/O 约束和 CDC 报告，导出 XSA。

仿真：`ax7010/pl/sim/tb_carrier_ctrl.v` 是自检测试平台（AXI 读写、PWM 与掉电强制关断、事件时间戳、按键消抖、编码器、WS2812 位时序、看门狗），已在 WSL 的 Icarus Verilog 完整运行通过。可从任意目录用 `sh ax7010/pl/sim/run_tests.sh` 执行，或在 sim 目录运行：`iverilog -g2005 -o tb ../rtl/*.v IOBUF.v tb_carrier_ctrl.v && vvp tb`。

SPI 时钟按 FCLK 自动选不超过 1 MHz 的分频（100 MHz 时为 781 kHz）。复位后 PWM 和 WS_DATA 为低，片选为高。PFAIL_N 变低或看门狗超时时，PL 直接关闭两路补光 PWM；看门狗超时还会发送故障帧（HL101 红）。PFAIL_N 为低（且 PWM_CTRL[4] 掉电保护开启）时 `pfail_kill` 同时停掉麦克风时钟，重新使能接收核才恢复。

| 偏移 | 名称 | 说明 |
|---|---|---|
| 0x000/004/008 | ID/VERSION/CLK_HZ | 0x56424331 / 0x00010000 / 时钟频率 |
| 0x010/014 | TS_LO/TS_HI | 读 TS_LO 时锁存 TS_HI |
| 0x020 | IN_RAW | [10:0] 事件源电平，[11] k1，[12] enc_sw，[13] A，[14] B |
| 0x024/028/02C | KEY_STATE/ENC_COUNT/ENC_ERRORS | 按下状态；×4 计数；非法跳变数 |
| 0x030/034/038 | DEBOUNCE_MS/LONG_MS/ENC_FILTER_US | 默认 15 / 3000 / 200 |
| 0x040–0x050 | PWM_PERIOD/DUTY0/DUTY1/CTRL/STATUS | CTRL[0][1] 使能，[4] 掉电强制关，[5] 看门狗强制关；STATUS 写 1 清除 |
| 0x060/064/068 | WDG_TIMEOUT_MS/KICK/STATUS | 0 = 关闭 |
| 0x080–0x0C0 | WS_CTRL/STATUS/RESET_US/COLOR0–4/FAULT0–4 | 颜色 0x00RRGGBB，HL101 为 0 号 |
| 0x100–0x12C | EVT_STATUS/INFO/TS_LO/TS_HI/POP/DROPPED/CLEAR/EN_RISE/EN_FALL/IRQ_CTRL/IRQ_STATUS | 事件 FIFO 512 项，每项带 64 位时间戳和序号 |

事件源编号：0 IMU_INT1，1 IMU_INT2，2 ADXL_DRDY，3 TOF_INT，4 RTC_1HZ，5 TACH_IN，6 PFAIL_N，7 EXP_INT_N，8 ESP_IRQ，9 PRN_CTS，10 ESP_CTS，16 K1 按下/松开，17 ENC_SW，18 K1 长按，19 ENC_SW 长按，20 编码器步进（level = 方向）。

## AX7010 Linux 程序

构建：PC 上 `make test` 编译并运行全部主机测试；目标板在 PetaLinux SDK 环境中 `make`（CXX/CXXFLAGS 由 SDK 提供）。不用 SDK 时也可在 Ubuntu 上交叉编译静态程序（2026-10-21 已验证：全部程序和测试编译无警告，未在板上运行）：`apt install g++-arm-linux-gnueabihf` 后执行 `make BUILD=build_arm CXX=arm-linux-gnueabihf-g++ CC=arm-linux-gnueabihf-gcc AR=arm-linux-gnueabihf-ar CXXFLAGS="-O2 -mcpu=cortex-a9 -mfpu=neon -mfloat-abi=hard" CFLAGS="-O2 -mcpu=cortex-a9 -mfpu=neon -mfloat-abi=hard" LDFLAGS=-static all`（CFLAGS 用于多点测振的 C 核心）。静态链接后不依赖板上 C++ 库版本，复制到任意 ARM Linux 镜像即可运行；没有载板时 vibmond 跳过载板外设。`make integration-test` 运行应用状态机回归与合成相机端到端流程。

部署：把 `petalinux/recipes-apps/vibmon` 复制到 project-spec/meta-user/recipes-apps/，用 `tools/make_src_tarball.sh` 生成 `files/vibmon-src.tar.gz`，加入 rootfs。内核选项见 `petalinux/vibmon-kernel.cfg`。设备树按 `petalinux/system-user.dtsi` 合并（含 `add_carrier.tcl`、`add_stereo.tcl` 生成的两个 dtsi），并在内核命令行追加 `uio_pdrv_genirq.of_id=vibmon,uio`（载板控制与双目核两个 UIO 节点共用）。配方用 Yocto 新语法（`:${PN}`），PetaLinux 2021 及以前改为 `_${PN}`。

运行：`vibmond -c /etc/vibmon/vibmon.json`（模板 `config/vibmon.json`）。LED1（J301）接全部照明灯，PWM 调光；LED2（J304）接 12V、650nm 十字线投影器，固定 100% 输出、只开关；两路都受 SW301 硬件允许，掉电或看门狗超时由 PL 强制关闭。位移测量时默认自动关闭投影、结束后恢复；采集及分析期间拒绝重新开启，也不能改变该策略（`laser.off_during_measurement`），因为固定投影图案不保证随被测表面平移；记录中 `lighting`、`laser_projector` 保存采集时的状态。屏幕设置页和 `set` 命令的修改保存在 `storage.settings_file`（默认 `/home/root/vibmon/settings.json`），启动时覆盖 vibmon.json 的同名项。程序按 AXI 基地址在 sysfs 中找 /dev/i2c-N、ttyS、spidev 和 uio 节点，不写死设备号。标定文件 `/etc/vibmon/calibration.json`（模板 `config/calibration.example.json`），没有有效标定时结果以像素为单位并显示“未判级”；报警阈值只用于毫米结果。无效测量不会清除先前报警。

| 程序 | 用途 |
|---|---|
| vibmond | 主程序：板内管理、补光与报警、前面板与五灯、传感器、Nano 链路、测量与记录、打印、触摸界面 |
| vibmonctl | 本地控制：`status`、`measure_start duration_s=10`、`baseline_start`（多点方式的停机基线）、`measure_stop`、`print`、`light on=true percent=80`、`laser on=true`、`mute`、`roi x=0.5 y=0.4 w=80 h=48 ref_x=0.1 ref_y=0.9`（中心为归一化坐标，宽高为像素，ref 为多点参考点组中心，可只给其中一部分）、`pan x=0.3 y=0.6`（放大时的视野中心，归一化坐标）、`stereo_distance distance_mm=1000 x=0.5 y=0.5`（该点目标的实际距离，校正双目水平偏心）、`voice asset=measure_done`、`set_net ssid=… password=…`、`rtc_set unix_ms=…`、`settings`、`set duration_s=30 warn_pp=0.5 alarm_pp=1 light_percent=80`、`shutdown` |
| vision_ref | 视觉处理参考：对录制片段运行 PS 算法，供与 PC 模型、PL 实现逐位比对（`pc_sim/check_bitexact.py` 调用） |
| carrier_test | 首板联调（先停 vibmond）：`probe`、`i2c-scan mgmt|sens`、`tca`、`load <名称> 0|1`、`power`、`rtc [set]`、`leds RRGGBB×5`、`pwm a b`、`events [秒]`、`imu`、`adxl`、`sht40`、`tof`、`nano [波特率]`、`printer [波特率]` |

测量记录：`records_dir/<测量编号>/record.json`（schema vision-vibration-record-1）、`waveform.csv`、`spectrum.csv`，有数据时还有 `imu.csv`、`reference_adxl345.csv`，启用声学时还有 `acoustic.wav`、`acoustic_spectrum.csv`。打印内容从保存的记录生成。测量开始时冻结测点与照明；丢帧、无效采集时刻、低质量跟踪、搜索触边和饱和不再插值成有效结果。峰峰值和有效值取去线性趋势后的位移；频谱峰值为单边 0-峰值（Hann 窗，已做相干增益修正）。

多点方式（`measurement.multipoint`：`max_points` 默认 12、`point_pitch_px` 32、`ref_pitch_px` 32、`baseline_s` 15、`baseline_dir`）：测点框内按间距布 25×25 模板网格，参考点组为 2×2 个背景模板（中心 `reference_roi.x/y`），至少 3 个有效参考的中位平移被扣除。`baseline_start` 在停机时采集基线，保存为 `baseline_dir/<布局散列>-<时间>.vbb`（不覆盖已有文件）；基线只对同一布局、分辨率和采样率有效（帧率差超过 1% 拒绝），测点框或参考点移动后需重采。测量时每 32 帧分析最近 128 帧（去趋势 Hann FFT、盲选峰、同频正弦拟合），各点该频率幅值达到基线包络 3 倍记为超过基线；结果主频为各 SIGNAL 窗口的中位数，各点 RMS 为图像平面二维谐波 RMS（像素，不换算毫米、不判级）。`record.json` 的 `result.multipoint` 记录布局散列、基线文件、窗口统计、每个测点的门限比与 RMS；`waveform.csv`、`spectrum.csv` 与像素指标对应门限比最大的代表测点。封装只对各模板当帧可能读到的区域做原算法的 3×3 模糊，结果与原包整帧模糊逐位一致（单元测试核对），PC 上每帧约 0.9 ms 对 10.5 ms；A9 上的每帧耗时尚未实测。

AN5642 双目相机（队友工程 `11_队友Vivado工程_20261021` 的 `stereo_hdmi.bit`）在 ALINX Debian 镜像上运行：先用 `tools/prepare_an5642_image.py` 改 image.ub，保留 DDR 顶部 32 MiB（相机六个帧存 0x1E000000 起 18 MiB、界面三帧 0x1F200000 起 6 MiB、双目 DMA 缓冲 0x1F800000 起 1 MiB）并重启；之后每次开机停掉桌面，`python3 tools/prepare_an5642_runtime.py <bit.bin>`（当前位流 `pl/stereo/bitstream/an5642_stereo_sgm40_pdm.bit.bin`，含双目核与麦克风接收核）经 /dev/xdevcfg 加载 PL、设 FCLK1 125 MHz、打开 PS I2C 时钟，再 `vibmond -c config/an5642_board.json`（相机 `pl_vdma` 45 fps，左机为 cam1；界面 `ui.display=pl_vdma` 由程序直接驱动 VDMA/VTC/dynclk 输出 1024×600）。双目匹配在位流含 `stereo_axi` 时经 /dev/mem 调用 PL 核（`pl_dma_base` 非 0），否则在 PS 上运行位精确模型（A9 单核约 0.64 s/对）。位流含 `pdm_mic` 时（`pl/bd/team_design.tcl` 生成）vibmond 经 /dev/mem 采集 J11-32/33 上的 MP34DT01（没有载板时可把模块直接接 J11，但 DAT 没有上拉，未接模块时读数不确定）；位流不含它时设备页显示“PL 位流不含麦克风接收核”。`config/an5642_stereo_nominal.json` 不是棋盘格标定：焦距按 3.6 mm 镜头与 720p 模式 2.8 µm 输出像元取 1286 px，基线 40 mm（厂家规格），右目主点 (618, 390) 来自一对实拍图的块匹配（行差 30 px，远景视差多出约 22 px 的镜头偏心）；现场用视觉页“距离校正”或 `stereo_distance` 按已知距离修正，或换成 `pc_sim/stereo_calibrate.py` 的结果。微雪触摸屏需内核参数 `hid.ignore_special_drivers=1` 或运行时重新绑定 usbhid。除 image.ub 外不改 Flash 和其他启动文件；开机自启尚未配置。

VL53L1X 测距需要 ST 官方 ULD（STSW-IMG009）：把 `VL53L1X_api.c/.h`、`VL53L1X_calibration.c/.h` 放到 `ax7010/linux/vibmon/third_party/vl53l1x_uld/` 后重新编译；平台层已在 `src/drv/vl53l1x_uld_port`。没有 ULD 时只识别芯片型号，不输出距离。

中文字库由 `tools/gen_font.py`（需 Pillow，字体 Noto Sans SC Medium/Bold，SIL OFL）按源码里出现的字符生成 `src/gfx/font_data.inc`（中粗两种字重、14–44 像素 7 个字号）；界面或报告新增中文后重新运行。

## 首板需要确认的事项

| 事项 | 影响 |
|---|---|
| 厂家工程的 AN5642 是否提供 V4L2 设备、像素格式与帧率 | `camera` 配置；不是 V4L2 时需按 FrameSource 接口接入 |
| EM5820H 该批次波特率、TTL 针序、CTS 极性、完成确认方式 | `printer` 配置（status_query、ms_per_raster_row、各等待时间） |
| GY-291 的 SDO/CS/供电与共享 SPI 资格测试 | 通过前保持 `adxl345.enabled=false` |
| Nano 链路实测波特率与流控 | 两端统一后再提速 |
| 打印启动电流、12 V 跌落、PFAIL 阈值 | 打印与负载策略参数 |
