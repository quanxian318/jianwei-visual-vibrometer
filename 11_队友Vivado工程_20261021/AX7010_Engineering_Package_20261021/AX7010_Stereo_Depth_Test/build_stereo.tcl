set root D:/AX7010_Stereo_Depth_Test
set single D:/AX7010_2023.1/course_s2_vitis/22_ov5640_single/Vivado/auto_create_project
set dual D:/AX7010_2023.1/course_s2_vitis/24_an5642_lwip_double/Vivado/auto_create_project
create_project stereo_depth "$root/project" -part xc7z010clg400-1 -force
set_property ip_repo_paths "$single/my_ip" [current_project]
update_ip_catalog
create_bd_design design_1
create_bd_cell -type ip -vlnv xilinx.com:ip:processing_system7:5.5 processing_system7_0
source "$single/ps_config.tcl"
set_ps_config processing_system7_0
source "$single/pl_config.tcl"
set_property -dict [list CONFIG.PCW_I2C1_PERIPHERAL_ENABLE 1 CONFIG.PCW_I2C1_I2C1_IO EMIO] [get_bd_cells processing_system7_0]
set_property CONFIG.C_GPIO_WIDTH 2 [get_bd_cells cmos_rst]
set_property CONFIG.DEBUG_OV5640 false [get_bd_cells alinx_ov5640_0]
foreach cell {axi_vdma_0 axi_vdma_1} {
    set_property -dict [list CONFIG.c_num_fstores 3 CONFIG.c_enable_all 1] [get_bd_cells $cell]
}
create_bd_intf_port -mode Master -vlnv xilinx.com:interface:iic_rtl:1.0 cmos2_i2c
connect_bd_intf_net [get_bd_intf_ports cmos2_i2c] [get_bd_intf_pins processing_system7_0/IIC_1]
set cam2 [create_bd_cell -type ip -vlnv www.alinx.com.cn:user:alinx_ov5640:5.4 alinx_ov5640_1]
set_property CONFIG.DEBUG_OV5640 false $cam2
foreach name {pclk href vsync} {
    create_bd_port -dir I cmos2_$name
    connect_bd_net [get_bd_ports cmos2_$name] [get_bd_pins alinx_ov5640_1/cmos_$name]
}
create_bd_port -dir I -from 9 -to 0 cmos2_d
connect_bd_net [get_bd_ports cmos2_d] [get_bd_pins alinx_ov5640_1/cmos_d]
create_bd_cell -type ip -vlnv xilinx.com:ip:axis_subset_converter:1.1 axis_subset_converter_1
foreach prop [list_property [get_bd_cells axis_subset_converter_0]] {
    if {[string match CONFIG.* $prop]} {catch {set_property $prop [get_property $prop [get_bd_cells axis_subset_converter_0]] [get_bd_cells axis_subset_converter_1]}}
}
create_bd_cell -type ip -vlnv xilinx.com:ip:axi_vdma:6.3 axi_vdma_2
set_property -dict [list CONFIG.c_include_mm2s 0 CONFIG.c_include_s2mm_dre 1 CONFIG.c_m_axi_s2mm_data_width 64 CONFIG.c_num_fstores 3 CONFIG.c_enable_all 1 CONFIG.c_s2mm_genlock_mode 0 CONFIG.c_s2mm_linebuffer_depth 4096 CONFIG.c_s2mm_max_burst_length 128 CONFIG.c_use_s2mm_fsync 2] [get_bd_cells axi_vdma_2]
set_property CONFIG.NUM_SI 3 [get_bd_cells axi_mem_intercon]
set_property CONFIG.NUM_MI 6 [get_bd_cells processing_system7_0_axi_periph]
connect_bd_intf_net [get_bd_intf_pins alinx_ov5640_1/m_axis_video] [get_bd_intf_pins axis_subset_converter_1/S_AXIS]
connect_bd_intf_net [get_bd_intf_pins axis_subset_converter_1/M_AXIS] [get_bd_intf_pins axi_vdma_2/S_AXIS_S2MM]
connect_bd_intf_net [get_bd_intf_pins axi_vdma_2/M_AXI_S2MM] [get_bd_intf_pins axi_mem_intercon/S02_AXI]
connect_bd_intf_net [get_bd_intf_pins axi_vdma_2/S_AXI_LITE] [get_bd_intf_pins processing_system7_0_axi_periph/M05_AXI]
foreach pin {axi_vdma_2/s_axi_lite_aclk processing_system7_0_axi_periph/M05_ACLK} {connect_bd_net [get_bd_pins processing_system7_0/FCLK_CLK0] [get_bd_pins $pin]}
foreach pin {alinx_ov5640_1/m_axis_video_aclk axis_subset_converter_1/aclk axi_vdma_2/m_axi_s2mm_aclk axi_vdma_2/s_axis_s2mm_aclk axi_mem_intercon/S02_ACLK} {connect_bd_net [get_bd_pins processing_system7_0/FCLK_CLK1] [get_bd_pins $pin]}
foreach pin {axi_vdma_2/axi_resetn processing_system7_0_axi_periph/M05_ARESETN} {connect_bd_net [get_bd_pins rst_processing_system7_0_100M/peripheral_aresetn] [get_bd_pins $pin]}
foreach pin {alinx_ov5640_1/m_axis_video_aresetn axi_mem_intercon/S02_ARESETN} {connect_bd_net [get_bd_pins rst_processing_system7_0_150M/peripheral_aresetn] [get_bd_pins $pin]}
connect_bd_net [get_bd_pins xlconstant_1/dout] [get_bd_pins axis_subset_converter_1/aresetn]
assign_bd_address -offset 0x43020000 -range 0x00010000 -target_address_space [get_bd_addr_spaces processing_system7_0/Data] [get_bd_addr_segs axi_vdma_2/S_AXI_LITE/Reg] -force
assign_bd_address -offset 0 -range 0x40000000 -target_address_space [get_bd_addr_spaces axi_vdma_2/Data_S2MM] [get_bd_addr_segs processing_system7_0/S_AXI_HP0/HP0_DDR_LOWOCM] -force
validate_bd_design
save_bd_design
generate_target all [get_files design_1.bd]
add_files -norecurse [make_wrapper -files [get_files design_1.bd] -top]
set_property top design_1_wrapper [current_fileset]
add_files -fileset constrs_1 "$single/src/constraints/system.xdc"
add_files -fileset constrs_1 "$single/src/constraints/hdmi_out.xdc"
add_files -fileset constrs_1 "$dual/src/constraints/an5642.xdc"
update_compile_order -fileset sources_1
launch_runs synth_1 -jobs 4
wait_on_run synth_1
if {[get_property PROGRESS [get_runs synth_1]] ne "100%"} {error "Stereo synthesis failed"}
launch_runs impl_1 -to_step write_bitstream -jobs 4
wait_on_run impl_1
if {[get_property PROGRESS [get_runs impl_1]] ne "100%"} {error "Stereo implementation failed"}
file copy -force "$root/project/stereo_depth.runs/impl_1/design_1_wrapper.bit" "$root/stereo_hdmi.bit"
write_hw_platform -fixed -include_bit -force -file "$root/stereo_hdmi.xsa"
puts STEREO_HDMI_BUILD_SUCCESS
exit
