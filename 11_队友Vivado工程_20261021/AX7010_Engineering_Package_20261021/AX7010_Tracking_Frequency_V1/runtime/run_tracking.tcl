set root [file dirname [file normalize [info script]]]
set project [file dirname $root]
set f [open $project/BOARD_VERIFIED.txt r];set verification [read $f];close $f
set digest [string trim [exec powershell.exe -NoProfile -File $project/hash_file.ps1 $project/stereo_hdmi.bit]]
if {[string first "sha256=$digest" $verification]<0} {error "Verified bitstream hash mismatch"}
set fh [open $root/tracking_result.txt w]
proc log {s} {global fh;puts $s;puts $fh $s;flush $fh}
if {[catch {
 connect -url tcp:127.0.0.1:3121
 targets -set -nocase -filter {name =~ "APU"};rst -system;after 1500
 targets -set -nocase -filter {name =~ "ARM Cortex-A9 MPCore #0"};stop
 source $root/ps7_init.tcl;ps7_init;ps7_post_config
 targets -set -nocase -filter {name =~ "xc7z010"};fpga -file $project/stereo_hdmi.bit
 targets -set -nocase -filter {name =~ "ARM Cortex-A9 MPCore #0"};configparams force-mem-access 1
 dow $root/stereo_preview.elf;con;after 10000
 log "PREVIEW=[mrd -value 0x03002000 32]"
 targets -set -nocase -filter {name =~ "ARM Cortex-A9 MPCore #1"};stop
 dow $root/depth_worker.elf;rwr pc 0x02000000;con;after 3000
 set tr [mrd -value 0x03080000 32];set state [mrd -value 0x03002000 32]
 log "TRACK=$tr";log "STATE=$state"
 if {[lindex $tr 0]!=0x54524b32||[lindex $tr 1]!=0||[lindex $tr 2]!=0x50415353} {error "Tracker synthetic self-test failed: $tr"}
 if {[lindex $tr 3]<10} {error "Tracker is not sampling frames"}
 if {[lindex $state 3]!=0||[lindex $state 10]!=0||[lindex $state 11]!=0} {error "Video runtime error"}
 if {[lindex $tr 5]<5||[lindex $tr 6]<5} {error "Both real-image trackers must initialize and track"}
 set freq [mrd -value 0x030e0000 24];log "FREQUENCY=$freq"
 if {[lindex $freq 0]!=0x46524631||[lindex $freq 1]!=0||[lindex $freq 2]!=0x50415353} {error "Board frequency selftest failed"}
 log RESULT=SUCCESS
 disconnect
} message]} {
 log "RESULT=ERROR $message";catch {disconnect};close $fh;exit 1
}
close $fh
exit
