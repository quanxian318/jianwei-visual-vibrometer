# 已验证测试版本 · 2026-10-18

加载：deploy_reference_selection.log，RESULT=SUCCESS。跟踪与频谱合成自检通过。

真实数据：verification.json。同一窗口的四路FFT与NumPy复算最大幅度差均小于0.0001原生像素，采样约45Hz、无ROI拷贝丢弃，频谱批次耗时最大约0.95ms。这是数值一致性与运行性能检查，不是光学测量精度。

基线：baseline_result_20261018_175651/board_baseline.json。约31秒后就绪，四路各18个有效窗。当前固件没有精确导出基线起止时间；同目录entire_run_samples.csv是完整当前epoch运行数据，不冒称其全段都是静止基线。更早的175520目录有明确CORRECTION说明。

SHA256：

- stereo_hdmi.bit: D800FE8080982E69B0595B4610F3227B571CC9BD585B266405AE5FDE1BD386F7
- runtime/stereo_preview.elf: 64D173019266F63C285F1B448CAD639A8B279AE3710BD50B206D3494C9A793E8
- runtime/depth_worker.elf: D82F7AAEEE10519E6F9AFEB34F9681F36CF327D451A52F14B3792603C9C00156

不代表：振动实物频率已验证、排除了高频混叠、完成双目标定、具备微米精度、FFT已经用PL加速，或临时孔阵参考可靠于所有光照/场景。
