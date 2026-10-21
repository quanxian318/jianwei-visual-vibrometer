# V1.10 IC手册与官方Layout索引

核对日期：2026-10-15。主板14种IC（17颗U位号）和前面板WS2812B-V6均已有本地PDF；另保留传感器/开发板、MOSFET、连接器及电源应用资料。厂家核验目录现有36份PDF，原用户提供的AX7010与Arduino资料仍在各自目录。

**先打开[官方Layout图片浏览页](官方Layout截图/index.html)**，可直接查看图片，并跳回原始PDF相应页。页码均为PDF页序。共20页整页截图、9张局部图；已检查关键封装与图片截取范围。

其中7页是手册中的PCB布局示例，2页是MAX98357A同封装评估板铜层/丝印；其余明确分为通用连接/布线图、文字指南、典型电路和封装焊盘。图片为原PDF渲染/裁切，没有重绘。

## 板载IC与位号

| 当前位号 | 具体型号 | 封装 | 本地手册 | 官方Layout/图片性质 |
|---|---|---|---|---|
| U101 | INA226AIDGSR | VSSOP-10 | [INA226.pdf](厂家核验_2026-10-15/INA226.pdf) | PDF31页；Figure 8-4，分流取样与旁路布局 |
| U102 | TLV3011BIDBVR | DBV / SOT-23-6 | [TLV3011B.pdf](厂家核验_2026-10-15/TLV3011B.pdf) | PDF28页；Figure 10-7布局 |
| U201/U202/U203 | TPS5430DDAR | DDA / SO-PowerPAD-8 | [TPS5430.pdf](厂家核验_2026-10-15/TPS5430.pdf) | PDF24页；Figure 7-12；热焊盘和高di/dt回路 |
| U204 | TLV1117LV33DCYR | DCY / SOT-223 | [TLV1117LV.pdf](厂家核验_2026-10-15/TLV1117LV.pdf) | PDF15页；Figure 8-4；大焊盘是VOUT |
| U205 | TPS7A2033PDBVR | DBV / SOT-23-5 | [TPS7A20.pdf](厂家核验_2026-10-15/TPS7A20.pdf) | PDF31页；Figure 7-7；本项目用DBV，不用DQN/WLCSP |
| U301/U302 | UCC27517DBVR | DBV / SOT-23-5 | [UCC27517.pdf](厂家核验_2026-10-15/UCC27517.pdf) | PDF23页；Figure 29，本项目封装；不要使用22页UCC27516DRS |
| U401 | 74LVC244APW,118 | PW / TSSOP-20 | [74LVC244A.pdf](厂家核验_2026-10-15/74LVC244A.pdf) | 本地为Nexperia Rev.9旧版镜像；没有外围PCB实例。封装尺寸见PDF12页；当前原厂Rev.14链接另列 |
| U402 | SN74LVC1G125DBVR | DBV / SOT-23-5 | [SN74LVC1G125.pdf](厂家核验_2026-10-15/SN74LVC1G125.pdf) | PDF15页通用旁路/转角图；27页是DBV5封装焊盘 |
| U501 | SN74LVC2G17DBVR | DBV / SOT-23-6 | [SN74LVC2G17.pdf](厂家核验_2026-10-15/SN74LVC2G17.pdf) | PDF11页通用输入端接图，不是PCB；31页是DBV6封装焊盘 |
| U502 | SN74AHCT1G125DBVR | DBV / SOT-23-5 | [SN74AHCT1G125.pdf](厂家核验_2026-10-15/SN74AHCT1G125.pdf) | PDF11页通用输入端接图，不是PCB；18页是DBV5封装焊盘 |
| U601 | SN74LVC2G125DCUT | DCU / VSSOP-8 | [SN74LVC2G125.pdf](厂家核验_2026-10-15/SN74LVC2G125.pdf) | PDF14页通用端接/转角图；25页是DCU8封装焊盘 |
| U602 | MAX98357AETE+T | ETE / TQFN-16 | [MAX98357A.pdf](厂家核验_2026-10-15/MAX98357A.pdf) | 主手册PDF33页文字指南；另存TQFN评估板PDF7–8页实际四层PCB |
| U801 | TCA9555PWR | PW / TSSOP-24 | [TCA9555.pdf](厂家核验_2026-10-15/TCA9555.pdf) | PDF31页；Figure 43布局 |
| U802 | DS3231SN#T&R | SO-16W | [DS3231.pdf](厂家核验_2026-10-15/DS3231.pdf) | PDF18页装配/布局文字指南；没有走线实例 |
| 小板HL101–HL105 | WS2812B-V6 | 5050 LED | [WS2812B-V6.pdf](厂家核验_2026-10-15/WS2812B-V6.pdf) | PDF5页仅典型级联电路，没有官方PCB布局 |

## 画PCB时优先参考

1. **TPS5430**：输入陶瓷电容、开关、续流二极管形成的高频回路紧凑；PH/SW铜皮不过度铺大；反馈远离开关节点；PowerPAD按原厂要求接地和散热。三路分别就近放置自己的输入、输出元件。
2. **UCC27517**：使用PDF23页的DBV图；驱动电源旁路和Gate/Source回路短，驱动回流靠近MOSFET Source。不要把大电流负载回流串进逻辑输入回流。
3. **INA226**：两条取样线从分流电阻焊盘内侧独立引出，靠近并行；不从功率铜皮远端取样。原厂图里的示例网名不是本项目网名，不据此更改本项目连接。
4. **TLV1117LV**：SOT-223的Tab连接VOUT。散热铜皮按VOUT处理，不要误接GND。TPS7A20使用DBV5图，另页WSON/WLCSP图不适用。
5. **MAX98357A**：本项目是TQFN16，主手册PDF34页的WLP实例不使用。改看[原厂TQFN评估板手册](厂家核验_2026-10-15/MAX98357A-TQFN-EVK.pdf)的7–8页。评估板为四层，供电旁路、裸露焊盘接地及输出回路可参考；不要直接照抄评估板声道、增益跳线数值。本项目仍按V1.10原理图连接。
6. **DS3231与逻辑器件**：去耦就近、地回路连续；未用CMOS输入按方案固定电平。DS3231内有晶体，按PDF18页处理封装下走线及清洗。逻辑器件通用端接图不等于PCB模板。
7. **前面板**：WS2812每颗就近去耦；按ENC101和K101实际操作位置留机械固定孔、旋钮帽与按钮柱空间。

以上是把官方资料对应到当前方案的绘图提示，未进行实际PCB布局或DRC审查。

## 74LVC244A版本说明

Nexperia当前原厂文件的在线版本为Rev.14（2024-11-29）；直接下载地址在本网络返回403。本地已保存并核对厂标/版权的是RS镜像中的**Nexperia Rev.9（2018-08-13），16页**，用作引脚、封装及基础参数参考。不能把这份旧版称作最新版本，也不以它替代前次电气核查对当前原厂文本的依据。

- [当前Nexperia原厂文件链接](https://assets.nexperia.com/documents/data-sheet/74LVC_LVCH244A.pdf?1=)
- [本地Rev.9](厂家核验_2026-10-15/74LVC244A.pdf) / [RS原文件镜像](https://docs.rs-online.com/61c3/0900766b816ae41e.pdf)

## 外接模块与传感器资料

| 模块 | 本地资料 | 使用范围 |
|---|---|---|
| AX7010 | [AX7010_UG.pdf](AX7010/AX7010_UG.pdf) | 开发板接口、电源与板卡结构；原用户资料 |
| Arduino Nano ESP32 ABX00083 | [ABX00083-datasheet.pdf](Arduino_Nano_ESP32/ABX00083-datasheet.pdf)、[ESP32-S3](厂家核验_2026-10-15/ESP32-S3.pdf) | 完整开发板按Arduino文档，芯片手册作补充 |
| PN532 V3 | [模块V3使用手册](厂家核验_2026-10-15/PN532-module-V3.pdf)、[NXP PN532/C1](厂家核验_2026-10-15/PN532.pdf) | 模块接口/模式设置与芯片协议；NXP原PDF由LilyGO模块厂家仓库镜像取得 |
| 黑色VL53LXX-V2 | [VL53L1X](厂家核验_2026-10-15/VL53L1X.pdf)、[VL53L0X](厂家核验_2026-10-15/VL53L0X.pdf) | 黑板可能装不同芯片，以实物/ID判定；不是两份驱动同时套用 |
| LSM6DSV模块 | [LSM6DSV](厂家核验_2026-10-15/LSM6DSV.pdf) | SPI及中断时序；传感器安在相机机械支架上 |
| SHT40模块 | [SHT4x](厂家核验_2026-10-15/SHT4x.pdf) | Sensirion Version 7.3；本版用模块，裸芯片焊盘不当作载板布局示例 |
| 可选ADXL355 | [ADXL354/ADXL355 Rev.D](厂家核验_2026-10-15/ADXL355.pdf) | 原厂45页；从ADI分段下载，已校验PDF完整性；本版为外接可选模块 |

外接模块内部的裸芯片Layout不要求重新在载板复刻；载板重点是连接器针序、去耦、线束和模块机械/天线空间。首版热成像仅预留接口，不要求购买MLX90640。

## 所有已下载PDF及来源

原厂网站可以直接下载的优先采用原站；镜像保存的是制造商编写的原PDF，来源链接按实际下载地址记录。每份文件页数及SHA256在[manifest.json](厂家核验_2026-10-15/manifest.json)中。

| 文件 | PDF页数 | 下载来源 |
|---|---:|---|
| [2N7002-CJ.pdf](厂家核验_2026-10-15/2N7002-CJ.pdf) | 5 | [来源](https://www.jscj-elec.com/uploads/20250424/0489ad6f192877ecd9e7a5d7fc98ba15.pdf) |
| [74LVC244A.pdf](厂家核验_2026-10-15/74LVC244A.pdf) | 16 | [来源](https://docs.rs-online.com/61c3/0900766b816ae41e.pdf) |
| [ADXL355.pdf](厂家核验_2026-10-15/ADXL355.pdf) | 45 | [来源](https://www.analog.com/media/en/technical-documentation/data-sheets/adxl354_adxl355.pdf) |
| [AO3400A.pdf](厂家核验_2026-10-15/AO3400A.pdf) | 5 | [来源](https://www.aosmd.com/sites/default/files/res/datasheets/AO3400A.pdf) |
| [AO4407A.pdf](厂家核验_2026-10-15/AO4407A.pdf) | 5 | [来源](https://www.aosmd.com/sites/default/files/res/datasheets/AO4407A.pdf) |
| [AOD4184A.pdf](厂家核验_2026-10-15/AOD4184A.pdf) | 6 | [来源](https://www.aosmd.com/sites/default/files/res/datasheets/AOD4184A.pdf) |
| [DS187.pdf](厂家核验_2026-10-15/DS187.pdf) | 73 | [来源](https://assets.testequity.com/te1/Documents/pdf/digilent/ds187-XC7Z010-XC7Z020-Data-Sheet.pdf) |
| [DS3231.pdf](厂家核验_2026-10-15/DS3231.pdf) | 20 | [来源](https://www.analog.com/media/en/technical-documentation/data-sheets/DS3231.pdf) |
| [ESP32-S3.pdf](厂家核验_2026-10-15/ESP32-S3.pdf) | 87 | [来源](https://www.espressif.com/sites/default/files/documentation/esp32-s3_datasheet_en.pdf) |
| [INA226.pdf](厂家核验_2026-10-15/INA226.pdf) | 41 | [来源](https://www.ti.com/lit/ds/symlink/ina226.pdf) |
| [JST-GH.pdf](厂家核验_2026-10-15/JST-GH.pdf) | 6 | [来源](https://www.jst-mfg.com/product/pdf/eng/eGH.pdf) |
| [JST-PH.pdf](厂家核验_2026-10-15/JST-PH.pdf) | 5 | [来源](https://www.jst-mfg.com/product/pdf/eng/ePH.pdf) |
| [LSM6DSV.pdf](厂家核验_2026-10-15/LSM6DSV.pdf) | 189 | [来源](https://www.st.com/resource/en/datasheet/lsm6dsv.pdf) |
| [MAX98357A.pdf](厂家核验_2026-10-15/MAX98357A.pdf) | 38 | [来源](https://www.analog.com/media/en/technical-documentation/data-sheets/MAX98357A-MAX98357B.pdf) |
| [MAX98357A-TQFN-EVK.pdf](厂家核验_2026-10-15/MAX98357A-TQFN-EVK.pdf) | 9 | [来源](https://www.analog.com/media/en/technical-documentation/data-sheets/MAX98357DEVTQFN-MAX98357EVSYSTQFN.pdf) |
| [MF-MSMF.pdf](厂家核验_2026-10-15/MF-MSMF.pdf) | 16 | [来源](https://www.bourns.com/docs/product-datasheets/mf-msmf.pdf) |
| [PJ-002BH.pdf](厂家核验_2026-10-15/PJ-002BH.pdf) | 3 | [来源](https://www.sameskydevices.com/product/resource/pj-002bh.pdf) |
| [PN532.pdf](厂家核验_2026-10-15/PN532.pdf) | 222 | [来源](https://raw.githubusercontent.com/Xinyuan-LilyGO/T-Embed-CC1101/master/hardware/PN532_C1.pdf) |
| [PN532-module-V3.pdf](厂家核验_2026-10-15/PN532-module-V3.pdf) | 13 | [来源](https://www.elechouse.com/elechouse/images/product/PN532_module_V3/PN532_%20Manual_V3.pdf) |
| [SCT1032.pdf](厂家核验_2026-10-15/SCT1032.pdf) | 6 | [来源](https://www.setfuse.cn/SdwlUploads/Sy/Mini%20Fuses_SMFL_SCT1032_CHN_SETsafe%20%26%20SETfuse.pdf) |
| [SHT4x.pdf](厂家核验_2026-10-15/SHT4x.pdf) | 25 | [来源](https://sensirion.com/resource/datasheet/sht4x) |
| [SLVA716A.pdf](厂家核验_2026-10-15/SLVA716A.pdf) | 24 | [来源](https://www.ti.com/lit/an/slva716/slva716.pdf) |
| [SN74AHCT1G125.pdf](厂家核验_2026-10-15/SN74AHCT1G125.pdf) | 26 | [来源](https://www.ti.com/lit/ds/symlink/sn74ahct1g125.pdf) |
| [SN74LVC1G125.pdf](厂家核验_2026-10-15/SN74LVC1G125.pdf) | 52 | [来源](https://www.ti.com/lit/ds/symlink/sn74lvc1g125.pdf) |
| [SN74LVC2G125.pdf](厂家核验_2026-10-15/SN74LVC2G125.pdf) | 30 | [来源](https://www.ti.com/lit/ds/symlink/sn74lvc2g125.pdf) |
| [SN74LVC2G17.pdf](厂家核验_2026-10-15/SN74LVC2G17.pdf) | 36 | [来源](https://www.ti.com/lit/ds/symlink/sn74lvc2g17.pdf) |
| [SRP1038A.pdf](厂家核验_2026-10-15/SRP1038A.pdf) | 5 | [来源](https://www.bourns.com/docs/product-datasheets/srp1038a.pdf) |
| [TCA9555.pdf](厂家核验_2026-10-15/TCA9555.pdf) | 49 | [来源](https://www.ti.com/lit/ds/symlink/tca9555.pdf) |
| [TLV1117LV.pdf](厂家核验_2026-10-15/TLV1117LV.pdf) | 25 | [来源](https://www.ti.com/lit/ds/symlink/tlv1117lv.pdf) |
| [TLV3011B.pdf](厂家核验_2026-10-15/TLV3011B.pdf) | 43 | [来源](https://www.ti.com/lit/ds/symlink/tlv3011b.pdf) |
| [TPS5430.pdf](厂家核验_2026-10-15/TPS5430.pdf) | 41 | [来源](https://www.ti.com/lit/ds/symlink/tps5430.pdf) |
| [TPS7A20.pdf](厂家核验_2026-10-15/TPS7A20.pdf) | 63 | [来源](https://www.ti.com/lit/ds/symlink/tps7a20.pdf) |
| [UCC27517.pdf](厂家核验_2026-10-15/UCC27517.pdf) | 37 | [来源](https://www.ti.com/lit/ds/symlink/ucc27517.pdf) |
| [VL53L0X.pdf](厂家核验_2026-10-15/VL53L0X.pdf) | 38 | [来源](https://www.st.com/resource/en/datasheet/vl53l0x.pdf) |
| [VL53L1X.pdf](厂家核验_2026-10-15/VL53L1X.pdf) | 36 | [来源](https://www.st.com/resource/en/datasheet/vl53l1x.pdf) |
| [WS2812B-V6.pdf](厂家核验_2026-10-15/WS2812B-V6.pdf) | 8 | [来源](https://atta.szlcsc.com/upload/public/pdf/source/20251201/45CD695EA06F90F53A3AC4760AC4DC7D.pdf) |

NKK/TE机壳开关机械图仍可通过采购清单中的原厂在线链接查看；本次没有将下载失败的文件计入36份PDF。完整装配采购需求见[PCB之外采购清单](../00_当前设计方案/PCB之外采购清单.md)。
