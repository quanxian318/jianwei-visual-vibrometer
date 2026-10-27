# 当前PL重建所需基础工程

- `AX7010_Sdoira_PL_V1/project/stereo_depth.xpr`：与当前相机/HDMI设计对应的基础工程，含相邻rtl和vendor依赖。
- `AX7010_2023.1`：该XPR引用的厂家IP/约束目录。两个目录必须保持相邻，不能只复制一个XPR。

文件内容未改。当前生成流程为`../pl/bd/team_design.tcl`，它在上述基础上执行双目替换、PDM加入及构建脚本；最终位流在`../pl/stereo/bitstream`。Vivado工具链对中文路径有限制，实际构建仍复制到ASCII工作目录；让该目录包含这两个基础工程、team_*.tcl、stereo_rtl、audio_rtl和pdm_mic_team.xdc，再执行team_design.tcl。

载板控制与Nano UART等另在`../pl/bd/add_carrier.tcl`，尚需与当前相机设计集成、约束和实板联调。不能把这里保存的基础bit/DCP直接当作完整载板应用固件。
