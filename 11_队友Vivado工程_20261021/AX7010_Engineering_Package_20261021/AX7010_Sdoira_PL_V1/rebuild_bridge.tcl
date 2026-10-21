set root [file dirname [file normalize [info script]]]
open_project $root/project/stereo_depth.xpr
add_files [glob $root/rtl/*.v]
update_compile_order -fileset sources_1
open_bd_design [get_files design_1.bd]
# Top-level ports are unchanged; rebuild the module reference from updated RTL.
validate_bd_design
save_bd_design
generate_target all [get_files design_1.bd]
reset_runs design_1_sdoira_bm_axi_0_0_synth_1
reset_runs synth_1
launch_runs synth_1 -jobs 4
wait_on_run synth_1
if {[get_property PROGRESS [get_runs synth_1]] ne "100%"} {error "Bridge synthesis failed"}
launch_runs impl_1 -to_step write_bitstream -jobs 4
wait_on_run impl_1
if {[get_property PROGRESS [get_runs impl_1]] ne "100%"} {error "Bridge implementation failed"}
file copy -force $root/project/stereo_depth.runs/impl_1/design_1_wrapper.bit $root/stereo_hdmi.bit
write_hw_platform -fixed -include_bit -force -file $root/stereo_hdmi.xsa
puts SDOIRA_ELASTIC_BUILD_SUCCESS
exit
