# Nano ESP32开发实施说明

目标板是Arduino Nano ESP32 ABX00083，使用USB-C烧录；软件框架按Arduino。Nano管理Wi-Fi/NTP、PN532、主载板MicroSD和MAX98357A语音；前面板、打印机和J701～J706传感器由AX控制。

## 工程先配置什么

选择与ABX00083对应的Nano ESP32板定义，记录板包名称、版本、底层ESP32 core、分区方案和库版本。当前包没有编译过的固件，也没有替你锁定一组不存在的工具版本。先编译USB日志＋GPIO空载最小程序，保留成功版本，再集成各外设。

Arduino API中使用D2、D3、A0等符号，不能把数字5同时当“D5”和“GPIO5”。GPIO5实际是D2。底层ESP-IDF驱动需要原始GPIO时用GPIO表单独转换；某些Arduino库参数会进行数字引脚映射，逐个检查库API，不能混用两套编号。[Arduino官方编号说明](https://support.arduino.cc/hc/en-us/articles/10483225565980-Select-pin-numbering-for-Nano-ESP32-in-Arduino-IDE)。

完整30针表见[Nano逐针定义](Nano逐针定义.md)；可复制[board_pins.h](board_pins.h)中的常量。该头文件只定义硬件连接，不是完成的驱动或已经实板跑过的程序。

## 所有已使用信号

| 功能 | Arduino符号 | ESP32-S3 GPIO | 方向/电平 |
|---|---|---|---|
| 发往AX | D2 | 5 | UART TX，空闲高 |
| 接收AX | D3 | 6 | UART RX |
| Nano RTS | D4 | 7 | 发往AX CTS，低允许 |
| Nano CTS | D5 | 8 | 来自AX RTS，低允许 |
| 事件通知AX | D6 | 9 | 输出，高表示队列非空 |
| I2S BCLK | D7 | 10 | 输出 |
| I2S LRCLK | D8 | 17 | 输出 |
| I2S SDOUT→MAX98357A DIN | D9 | 18 | 输出 |
| SD片选 | D10 | 21 | 输出，低有效 |
| SPI MOSI | D11 | 38 | 输出 |
| SPI MISO | D12 | 47 | 输入，经U601 |
| SPI SCLK | D13 | 48 | 输出，同时是LED_BUILTIN，不拿它做心跳闪灯 |
| PN532 IRQ | A0 | 1 | 输入，按模块IRQ行为处理 |
| SD_CD | A1 | 2 | 输入，低表示插卡 |
| PN532片选 | A3 | 4 | 输出，低有效 |
| 掉电预警 | A6 | 13 | 输入，低有效 |
| AMP_EN | A7 | 14 | 输出，高启用功放 |
| J605调试TX/RX | D1/D0 | 43/44 | TTL 3.3V；与AX串口不同 |

Nano VIN由主板12V_MAIN经F201供电，3V3和VBUS引脚没有接到载板电源；不要并接Nano 3V3与AX_3V3。USB调试时按装配指南给载板供电，避免对未供电模块输出信号。

## 启动顺序

1. 最先AMP_EN=0、SD_CS=1、PN_CS=1、事件IRQ=0。建立USB日志，打印固件版本/board profile/boot_id。
2. 初始化事件队列、串口接收环形缓冲、SPI互斥和看门狗；注册掉电输入。此时尚不连接网络、不播放声音。
3. AX链路先115200、8N1；RX=D3、TX=D2，CTS=D5、RTS=D4。按所选core的HardwareSerial API配置UART1和硬件流控，不能使用默认Serial0针脚猜测连接。库的setPins顺序以对应版本文档为准；常见API是RX、TX、CTS、RTS。
4. 两CS保持高后初始化SPI，显式传入SCLK=D13、MISO=D12、MOSI=D11。分别初始化SD和PN532，失败则上报独立模块故障，保留串口服务。
5. 初始化I2S但保持功放关闭；检查语音文件清单。按配置连接Wi-Fi并启动NTP任务。响应HELLO后上报能力/模块状态。
6. 验证流控后双方同时切到目标921600；不是在连接中途单端自行切换。正式版本将选定波特率记录为共同配置。

RX任务不得因Wi-Fi连接失败、PN532等待卡或文件播放而长期阻塞。所有等待有超时，任务错误进入事件队列；掉电处理只发简短事件并停止启动新任务，不能在ISR里写SD。

## 建议的程序文件和任务

| 文件/服务 | 责任 |
|---|---|
| board_pins / board_profile | 唯一引脚表、SPI配置、功能开关 |
| link_transport | UART DMA/环形缓冲、协议收帧、CRC、重同步、RTS/CTS |
| command_router | HELLO会话、参数检查、请求去重、作业号与响应 |
| event_store | 有界事件队列、事件ID、IRQ电平、DRAIN/ACK |
| spi_bus | 全局互斥，管理事务速度/模式/位序和片选 |
| nfc_service | GetFirmwareVersion、SAM配置、轮询UID及标签离开检测 |
| sd_service | 插拔检测、挂载、WAV/配置读取、移除状态 |
| audio_service | WAV解析、I2S缓冲、AMP_EN、静音/完成/欠载事件 |
| net_time_service | 网络状态机、NTP校时质量和重连 |
| power_service | PFAIL锁存、禁止新任务、停止写卡/播放 |

若使用额外Arduino库，记录来源/版本及许可证，并审查SPI事务、GPIO编号和阻塞等待行为。不要将多个厂商示例中的setup/loop直接拼接。

## SD与PN532共享SPI的具体约束

只有一把SPI互斥锁；获取锁→两CS高→设置事务参数→只拉低目标CS→传输→目标CS高→结束事务→释放锁。传输期间不能由另一任务改全局位序。SD驱动可能自行管理CS，封装层不能在驱动内部再次拉低另一个CS。

| 设备 | 模式 | 位序 | 起步频率 | 备注 |
|---|---|---|---|---|
| SD | Mode 0 | MSB first | 初始化约400kHz，随后4MHz | SPI模式；与SD_CD机械检测分开 |
| PN532 | Mode 0 | LSB first | 1MHz | 不超过5MHz；模块切SPI拨码后重新上电 |

U601两组输出并到MISO_BUS，分别由对应CS低使能；两个CS同时低会造成输出冲突。PN532初始化不成功不能一直占总线。读取NFC标签时先读UID/type，事件按UID＋离开/重新出现去重，不能卡片停留时每次轮询都建立新测量。

SD检测A1低后消抖再挂载；初始化采用FAT32并在交付配置记录支持范围。拔卡先停止文件访问，音频任务报告SD_REMOVED，不继续读失效文件句柄。文件系统挂载失败不影响UART和Wi-Fi故障报告。

## 语音播放

首版约定语音文件为PCM WAV、16kHz、16bit signed little-endian、单声道。必须解析RIFF/WAVE及fmt/data块，跳过未知块和奇数字节填充，不能固定从第44字节读取所有WAV。按asset_id映射固定文件清单，主机不用任意SD路径执行任务。

MAX98357A接标准I2S。建议16kHz、两声道、每声道32bit slot，左右复制同一单声道样本，BCLK=16000×2×32=1.024MHz。将PCM16正确对齐到32bit有效数据位：用有符号样本扩大为int32后乘65536得到左对齐，再依所选I2S驱动规定的内存声道顺序提交；避免对负数直接做C有符号左移。I2S MSB相对WS切换延迟一位，不使用左对齐音频格式冒充标准I2S。

播放前预填DMA并维持零数据，随后AMP_EN=1；停止先淡出/补零，再AMP_EN=0。R617为510k，硬件选择左右平均通道，复制L/R避免只播一边导致音量变化。欠载计数、SD读取失败、作业取消、自然结束分别上报。

建议先读入两个以上音频块做双缓冲，SPI锁只覆盖SD块读取，不跨越整段播放。NFC和SD调度以音频缓冲水位为依据；不在中断里读卡。语音文件列表见[模板](voice_assets.csv)，这里只定义待录制文件名，包内没有伪造录音。

## Wi-Fi与时间

网络连接和重连采用有限超时/退避，不影响测量串口。联网状态事件包括IP可用与时间同步状态，不把“已连接AP”等同于互联网或NTP可用。NTP返回UTC毫秒及采样时刻/估计不确定度，AX按政策更新系统与DS3231；测量采样始终使用各自单调时钟。

SET_NET可在串口首次配置Wi-Fi，日志不能回显密码。保存配置在空闲期完成；掉电预警后拒绝新持久化操作。若网络未配置，保持离线状态并允许NFC、语音和本地测量。

## 如何完成自测

先测UART HELLO/CRC错误恢复/长短分包/复位，再测SD和PN532分别工作，再持续交替访问SPI。以卡片反复靠近/移开同时循环播放WAV，检查音频欠载、帧丢失、两CS重叠和事件重复。最后测试SD拔出、无卡、网络断开、功放静音和PFAIL，确认失败事件能被AX收到。

打印和五灯不是Nano自测项目。没有AX软件时，可在PC上用本包Python编解码器生成测试帧，串口收发工具选择由开发环境提供；参考脚本不依赖额外pyserial。
