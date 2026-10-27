set root [file dirname [file normalize [info script]]]
create_project bm_core $root/core_project -part xc7z010clg400-1 -force
foreach xci [glob $root/vendor/ip/*/*.xci] {read_ip $xci}
foreach ip [get_ips] {if {[get_property IS_LOCKED $ip]} {upgrade_ip $ip}}
set_property generate_synth_checkpoint false [get_files *.xci]
generate_target all [get_ips]
add_files [glob $root/vendor/rtl/*.v]
add_files [glob $root/rtl/*.v]
set_property top sdoira_bm_axi [current_fileset]
synth_design -top sdoira_bm_axi -part xc7z010clg400-1 -mode out_of_context
create_clock -name bm_clk -period 10 [get_ports s00_axi_aclk]
create_clock -name bm_axi_clk -period 10 [get_ports m00_axi_aclk]
report_utilization -file $root/core_utilization.rpt
report_drc -file $root/core_drc.rpt
write_checkpoint -force $root/bm_core_synth.dcp
puts SDOIRA_CORE_SYNTH_COMPLETE
exit
