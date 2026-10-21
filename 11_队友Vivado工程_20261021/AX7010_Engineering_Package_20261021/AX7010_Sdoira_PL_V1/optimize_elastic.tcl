set root [file dirname [file normalize [info script]]]
if {[catch {
 open_checkpoint $root/board_elastic_routed.dcp
 phys_opt_design -directive AggressiveExplore
 route_design -directive Explore
 report_timing_summary -file $root/elastic_optimized_timing.rpt -warn_on_violation
 report_bus_skew -file $root/elastic_optimized_bus_skew.rpt -warn_on_violation
 report_drc -file $root/elastic_optimized_drc.rpt
 set wns [get_property SLACK [get_timing_paths -delay_type max -max_paths 1]]
 set whs [get_property SLACK [get_timing_paths -delay_type min -max_paths 1]]
 write_checkpoint -force $root/board_elastic_optimized.dcp
 puts "ELASTIC_OPTIMIZED_TIMING setup=$wns hold=$whs"
 if {$wns<0 || $whs<0} {error "Optimized implementation timing failed $wns / $whs"}
 write_bitstream -force $root/stereo_hdmi.bit
 set digest [string trim [exec powershell.exe -NoProfile -File $root/hash_file.ps1 $root/stereo_hdmi.bit]]
 set f [open $root/BOARD_VERIFIED.txt w]
 puts $f "setup_slack=$wns hold_slack=$whs";puts $f "sha256=$digest";close $f
 puts SDOIRA_ELASTIC_BOARD_VERIFIED
} message options]} {
 puts "IMPLEMENTATION_ERROR=$message"
 puts "IMPLEMENTATION_DETAILS=[dict get $options -errorinfo]"
 exit 1
}
exit
