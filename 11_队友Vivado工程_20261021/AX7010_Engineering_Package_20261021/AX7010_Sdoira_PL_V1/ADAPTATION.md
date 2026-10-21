# AX7010 original sdoira RTL BM port — experimental V1

Source: user-provided XC7Z010_OV5640_BM_sdoira.zip, preserved reference at
D:/AX7010_BM_Reference_20261016/XC7Z010_OV5640_BM_sdoira.
Original author: sdoira. Original published project license: GPL-3.0-or-later.
This is not an ALINX official depth algorithm or RDK StereoNet port.

## Architecture

Existing AX7010 dual OV5640 capture / HDMI path retained. Original RTL BM,
SAD calculation, subpixel calculation, uniqueness filter, input/output buffers
and DDR arbiter reused. AXI-Lite control at 0x43C20000, DDR master via PS HP1.
Both original core clock inputs connect to the SAME 100 MHz FCLK0.
The video DDR AXI bus uses 125 MHz FCLK1 after the initial ~143 MHz build
missed setup by 0.504 ns (0.111 ns after post-route optimization).
This changes internal video data transport, not the sensor register settings
or HDMI pixel clock. Verify actual capture FPS after loading.
An explicit one-shot adapter trigger replaces the original X-Sobel-done pulse.
The BM datapath itself is not replaced by an ARM software matcher.

### Actual AXI port compatibility repair

The first on-board attempt had working registers, populated input memory and
44.9 FPS preview, but timed out with BM busy and zero completed frames. This
was NOT reported as successful deployment. The previous V7 runtime was restored.

The legacy arbiter requires AWREADY, WREADY and ARREADY simultaneously before
dispatching even a READ. The AXI width/protocol converter can keep WREADY low
until a write address exists. A new DDR simulation with address-before-data
readiness reproduced the same deadlock (busy=1, arb rd_state=0, no completion).

sdoira_axi_elastic.v adds independent AW/AR slots and one complete 256-beat
write-burst buffer. The reference arbiter remains one-transaction-at-a-time;
the external master now presents VALID without waiting for READY, retaining
payload until accepted. No BM cost/search algorithm is changed.

One transport fix in vendor/rtl/bm_ibuf.v gates bst_end with drd_vin. Original
code advanced past the final read beat when AXI paused at that count, losing
data. It is a burst-handshake correction, not a matching parameter change.

With both fixes, stalled-DDR simulation passes shift12=4114/4114,
shift48=4114/4114, uniform rejection=4114/4114, and textured zero disparity
4114/4114. See stall_before_console.txt
(reproduced failure) and stall_after_console.txt (corrected transport).
The deployment script now requires the stalled-DDR simulation to pass.
Adapter offsets 0x1A10/14/18/1C expose bridge status / accepted read addresses /
accepted read beats / accepted write beats for actual hardware diagnosis.
Updated standalone module synthesis: 6917 LUT, 17 BRAM equivalents.

ARM CPU1 supplies 320x180 grayscale (BGR byte order decoded with 29B+150G+77R),
a measured vertical-only alignment
approximation and raw X-Sobel clipped to [-32,31], biased by +32, matching
the original xsbl2.v limit() encoding, then triggers FPGA matching.
This approximation is NOT full stereo calibration. Foreign rectification
coefficients in the reference firmware are deliberately NOT applied.
Matching window 7, disparity range 64. Input DDR base 0x10000000, 2048-byte
row stride; each word packs L0,R0,L1,R1 from high byte to low byte.
SAD/output scratch base 0x14000000 (4 MB reserved). Final output tightly
packs 249x174 32-bit words, one best disparity/frac/SAD per pixel.
Final burst padding is at frame end, not after each row.
Uniqueness ratio is Q10; 921/1024 threshold, invalid disparity 255.
Shared display bytes encode valid hardware d=0..63 as d+1=1..64; byte0 is
invalid in the old display ABI. A measured zero disparity is therefore far
blue, not incorrectly discarded as an unmatched pixel. Raw dump is unchanged.
The original core latches filter settings at completion, so startup performs
two known-disparity self-tests before publishing live frames.

CPU0 renders 320x180 heat samples at 2x display scale. The software SGM matcher
is excluded from both executable link commands; palette.c is display only.
Preview and depth have independent actual frame counters.

## Verification so far

- Standalone XC7Z010 synthesis: 6605 LUT, 4442 FF, 17 BRAM36 equivalents, 1 DSP.
- AXI/DDR behavioral test: 192x40 random textured pair, exact shift 12 pixels.
  4114/4114 output disparities correct, no unknown values, AXI error flags zero.
  See sim_console.txt. This is synthetic correctness, NOT real-scene accuracy.
- Multi-frame / upper-disparity phase test: shift48 correct 4114/4114;
  frame 3 uniform input rejected 4114/4114 points
  through the original uniqueness filter. No artificial dense-fill stage.
- Initial board build fits (15244 LUT / 86.61%, 39.5 BRAM / 65.83%) but
  failed video-memory setup timing. It was NOT deployed.
- 125 MHz video-memory rebuild / real hardware validation still required.

## Build and test

run_vivado.ps1 build_core.tcl
run_vivado.ps1 simulate.tcl
run_vivado.ps1 build_board.tcl
run_vivado.ps1 verify_board.tcl
runtime/build.ps1
run_xsdb.ps1 runtime/run_stereo.tcl

Do not load a bitstream that failed timing. run_stereo.tcl checks the simulation
pass and BOARD_VERIFIED.txt. Only volatile JTAG/RAM loading is used, no Flash,
SD boot files, boot jumpers or existing shortcuts are modified.
Known-pair test results / runtime errors: 0x03030000, 8 words.
Word1 error: 1 timeout, 2 wrong core ID, 3 known-shift mismatch.
Word3 AXI faults; word4 core time in ARM global-timer ticks;
word5 BM FIFO status; word6 correct known-pair count; word7 tested count.
capture_result.tcl uses words8/9 as a request/ack handshake at a completed-frame
boundary, then reads the EXACT packed feature pair and hardware disparity.
This leaves CPU0 preview running and releases CPU1 even if host readout fails.

## Recovery

The existing working V7 files are untouched in D:/AX7010_Stereo_SGM_V7.
Use its run_stereo.tcl to reload the previous bitstream and both ARM programs.
baseline_stereo_hdmi.bit is also preserved here. Loading that bitstream alone
does not restore ARM software; use the complete original V7 runtime.

## Limits

No metric stereo calibration, frame-pair hardware synchronization or guaranteed
RDK-equivalent quality is claimed. Weak texture, occlusion, field overlap and
the disparity-search crop can still produce invalid pixels. Do not fill them
and label them as measured depth. Preview FPS is not the depth algorithm FPS.

## 2026-10-16 hardware transport revision and backup

The first 125 MHz build passed internal setup/hold checks but timed out on the
board. V7 was restored; the FPGA port has not yet passed a board trial.
An AXI slave model with independent address/data backpressure reproduced the
deadlock. The new sdoira_axi_elastic.v bridges the original arbiter to a
backpressured AXI interconnect. bm_ibuf.v also gates burst completion with a
valid read beat. With these transport fixes, all four stalled-DDR simulation
tests pass: shifts 12 and 48, uniform rejection, and valid zero disparity.

The revised full design uses 15542 LUTs (88.31%), 18751 FFs (53.27%), and
39.5 BRAM equivalents (65.83%). First routing completed, but setup slack was
-0.239 ns and hold slack +0.018 ns. This routed candidate was NOT loaded.
optimize_elastic.tcl attempts post-route optimization without changing clocks,
timing requirements, or the matching algorithm. Only a timing-passing candidate
may proceed to the guarded board self-test and real input/output readback.

The complete working V7 directory was archived to:
D:/AX7010_Archives/Stereo_SGM_V7_before_PL_20261016_2220.zip
Verified: 56 files, 3056595 uncompressed bytes.
SHA256: A7F5B6859568D21E9C586E2FD39B70884E27393508B1C5E0BC89ED60DBCFDD7C
No old project was deleted and no Flash was erased. JTAG reconfiguration
replaces the active FPGA design; deleting archived programs cannot free FPGA
logic resources in the currently loaded design.

## Successful elastic-bridge board trial

Post-route optimization passed the internal checks: setup +0.004 ns, hold
+0.018 ns; all four reported bus-skew constraints met; bit generation DRC had
zero errors. This is small setup margin, not a temperature/voltage soak test
or formal closure of all inherited camera/HDMI I/O constraints.
Final bit SHA256: D800FE8080982E69B0595B4610F3227B571CC9BD585B266405AE5FDE1BD386F7.

The AXI transport fixes passed on the board: known shifts 12 and 48 both
matched all 43326 output pixels. Live capture initially starved preview using
noncached byte reads. Word-aligned BGR reads and word-packed shared disparity
publication fixed the retry storm without changing BM costs or thresholds.
fifth_deploy.log records successful deployment, approximately 25 fps preview,
44-45 fps sensor capture, 25-26 fps depth computation. Heat colour rendering
is limited to at most 12 Hz independently of computation, as labelled on screen.
These preview results are below the previous 44 fps preview; do not advertise
them as 45 fps merely because sensor capture remains 45 fps.

Coherent hardware readback: runtime/hardware_result.json and .png. Second
capture contained 22269 valid measured pixels: 51.40% of the search ROI,
38.66% of the whole 320x180 input. All 201 sampled raw SAD costs matched
recomputation exactly, with crop (67,3), no byte/pair permutation. This validates
transport/indexing/cost computation, NOT disparity accuracy or RDK-like quality.
Black/gray invalid regions and noisy matching still exist. No artificial fill.

Start_Test.cmd launches this independent experimental build. Start_Test.ps1
attempts full V7 recovery if a later launch fails. Existing desktop launchers
and default firmware were not replaced. The old XSA was renamed
stereo_hdmi_pre_elastic_not_for_deployment.xsa because it embeds an obsolete
pre-bridge bitstream; this release uses the verified .bit and runtime ELFs.

stability_elastic.tcl subsequently passed five samples spaced three seconds
apart. Depth sequence kept advancing, preview 24.5-25.1 fps, capture about
44-45 fps, depth 25.4-25.6 fps, no reported video-DMA or FPGA AXI errors.
This short check is not a long-duration reliability test.

## V1.1 stability revision (2026-10-16)

User video 9e81b1d4aa8fb2d7a68b2f1c5ab2edce.mp4 (6.265 seconds,
189 frames) shows rapidly changing disparity colours, while camera preview,
text and colour bar are comparatively stable. This points to temporal
matching instability rather than a global backlight failure; phone video is
not a photometric proof. Four reruns of one frozen real input produced exactly
identical hardware output (SHA256 2EFCF4D4DC0872E4847715FB3883BF3C68868487C305EA454C11C14606F1D775),
with zero reported AXI errors. No RTL/timing changes in this revision.

The earlier worker chose camera 2's latest completed buffer only AFTER copying
camera 1. V1.1 snapshots both completed-buffer selections first and still
rejects overwritten frames. This reduces an avoidable software pairing skew;
it does NOT provide hardware synchronization or rectify the camera geometry.

depth_stability.h adds a spatial-support check (4 of 9 samples, disparity
difference <=2) and two-frame confirmation for large temporal changes. Small
changes are accepted immediately once established; invalid samples clear
history immediately. A still-valid large jump can retain an old value for
one frame only, and is explicitly a filtered display, not raw measured depth.
New patches require two valid consistent frames. No invalid pixel is filled.
Synthetic on-board self-tests cover initialization, a flat plane, isolated
outlier removal, recovery, bounded large-jump delay and invalid clearing.
report[16]=0x53545031 indicates self-test passed; error 4 indicates failure.
report[17..20] contain raw-valid, spatial-rejected, temporal-suppressed, and
filtered-valid counts. Raw FPGA readback remains available independently.

stability_fix_deploy.log: initial preview 29.9 fps, depth 20.8 fps, both sensors
about 45 fps, known-disparity tests 43326/43326, no FPGA or VDMA errors.
The extra confirmation delay is about one processed frame (~48 ms at 21 fps),
in addition to existing acquisition and display latency; not a measured
end-to-end latency. Some paired captures are safely rejected if overwritten.

One coherent static readback: raw-valid 22128, filtered-valid 10111, no filtered
nonzero samples at raw-invalid positions, no out-of-range disparity. Thus
noise suppression trades coverage for stability, and increases gray regions
in unstable areas. This is NOT evidence of improved metric depth accuracy.
runtime/stability_comparison.png/.json compare actual raw and filtered output.
Visual confirmation of reduced flicker is still required from the user.

Pre-fix firmware backup:
D:/AX7010_Archives/FPGA_BM_before_stability_fix_20261016.zip
SHA256 07A3335BEFF0183FB17497B1953F9D25367E00B1245D6471F375751FCFCFA7DF.
Existing Start_Test.cmd now uses the V1.1 runtime; defaults elsewhere unchanged.

## V1.2 adaptive cold-start confirmation

Second user video 0af4ebb0f6a5736a324c9e55394c4b2f.mp4 (188 frames,
6.2319 seconds) confirms less speckle flicker but large regions disappear.
A V1.1 readback had 31036 raw-valid and 22377 displayed pixels, including
2788 locally strongly supported (>=7 of 9) raw-valid pixels suppressed by
temporal history. V1.2 accepts such strongly supported newly visible patches
immediately. Weak patches still require two frames; established large jumps
still require confirmation, raw invalid clears history, no invalid filling.
The first V1.2 test run was rejected because its synthetic flat-plane expected
count was wrong (it omitted a valid boundary ring). The expected count was
corrected, not the filter relaxed to bypass a failing test. The subsequent
board run passed all filter tests and both FPGA known-disparity tests.
report[16]=0x53545032 identifies the passing adaptive self-test.

adaptive_fix_deploy_second.log records success (preview 28.6 fps, compute
20.7 fps at the initial sample). Later coherent readback had 27533 raw-valid,
20379 displayed, zero invented-invalid pixels, and only four strong-support
pixels withheld (these can still be established large temporal jumps).
Scenes differ between captures: these counts are not a controlled proof of
coverage improvement. User visual confirmation is required.

Near-field loss is not automatically a filter problem: current search is
64 disparities at 320x180 (about 4x sensor-coordinate disparity), with a
249x174 measured output crop. Shared field of view, occlusion, weak texture,
approximate alignment and unsynchronized exposure remain limitations.
No minimum distance in centimetres has been established: physical focal
length alone does not supply calibrated focal length in pixels.

Start_Test.cmd now launches V1.2; the full V1.1 release remains archived.
