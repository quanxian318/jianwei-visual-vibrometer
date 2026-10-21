set root [file dirname [file normalize [info script]]]
set f [open D:/AX7010_Stereo_Depth_Test/build_stereo.tcl r]
set original [read $f];close $f
set original [string map [list {set root D:/AX7010_Stereo_Depth_Test} "set root $root"] $original]
set cut [string first "validate_bd_design" $original]
eval [string range $original 0 [expr {$cut-1}]]
set_property CONFIG.PCW_FPGA1_PERIPHERAL_FREQMHZ 125 [get_bd_cells processing_system7_0]
foreach xci [glob $root/vendor/ip/*/*.xci] {read_ip $xci}
set_property generate_synth_checkpoint false [get_files *.xci]
foreach name {bm_calc_dly bm_obuf_fifo dpram_32_512 dpram_64_1024 fifo_frm tdpram_340_1024} {
 generate_target all [get_ips $name]
}
add_files [glob $root/vendor/rtl/*.v]
add_files [glob $root/rtl/*.v]
update_compile_order -fileset sources_1
set_property CONFIG.PCW_USE_S_AXI_HP1 1 [get_bd_cells processing_system7_0]
set_property CONFIG.NUM_MI 7 [get_bd_cells processing_system7_0_axi_periph]
create_bd_cell -type module -reference sdoira_bm_axi sdoira_bm_axi_0
puts "BM_INTERFACES=[get_bd_intf_pins sdoira_bm_axi_0/*]"
create_bd_cell -type ip -vlnv xilinx.com:ip:axi_interconnect:2.1 bm_mem_intercon
set_property -dict [list CONFIG.NUM_SI 1 CONFIG.NUM_MI 1] [get_bd_cells bm_mem_intercon]
connect_bd_intf_net [get_bd_intf_pins processing_system7_0_axi_periph/M06_AXI] [get_bd_intf_pins sdoira_bm_axi_0/s00_axi]
connect_bd_intf_net [get_bd_intf_pins sdoira_bm_axi_0/m00_axi] [get_bd_intf_pins bm_mem_intercon/S00_AXI]
connect_bd_intf_net [get_bd_intf_pins bm_mem_intercon/M00_AXI] [get_bd_intf_pins processing_system7_0/S_AXI_HP1]
foreach pin {sdoira_bm_axi_0/s00_axi_aclk sdoira_bm_axi_0/m00_axi_aclk processing_system7_0_axi_periph/M06_ACLK bm_mem_intercon/ACLK bm_mem_intercon/S00_ACLK bm_mem_intercon/M00_ACLK processing_system7_0/S_AXI_HP1_ACLK} {
 connect_bd_net [get_bd_pins processing_system7_0/FCLK_CLK0] [get_bd_pins $pin]
}
foreach pin {sdoira_bm_axi_0/s00_axi_aresetn sdoira_bm_axi_0/m00_axi_aresetn processing_system7_0_axi_periph/M06_ARESETN bm_mem_intercon/ARESETN bm_mem_intercon/S00_ARESETN bm_mem_intercon/M00_ARESETN} {
 connect_bd_net [get_bd_pins rst_processing_system7_0_100M/peripheral_aresetn] [get_bd_pins $pin]
}
create_bd_cell -type ip -vlnv xilinx.com:ip:xlconstant:1.1 bm_zero
set_property -dict [list CONFIG.CONST_WIDTH 8 CONFIG.CONST_VAL 0] [get_bd_cells bm_zero]
foreach pin {d1 d2} {connect_bd_net [get_bd_pins bm_zero/dout] [get_bd_pins sdoira_bm_axi_0/$pin]}
create_bd_cell -type ip -vlnv xilinx.com:ip:xlconstant:1.1 bm_zero_bit
set_property CONFIG.CONST_VAL 0 [get_bd_cells bm_zero_bit]
foreach pin {pclk1 pclk2 href1 href2 vsync1 vsync2} {connect_bd_net [get_bd_pins bm_zero_bit/dout] [get_bd_pins sdoira_bm_axi_0/$pin]}
assign_bd_address -offset 0x43c20000 -range 0x00004000 -target_address_space [get_bd_addr_spaces processing_system7_0/Data] [get_bd_addr_segs sdoira_bm_axi_0/s00_axi/reg0] -force
assign_bd_address -offset 0 -range 0x20000000 -target_address_space [get_bd_addr_spaces sdoira_bm_axi_0/m00_axi] [get_bd_addr_segs processing_system7_0/S_AXI_HP1/HP1_DDR_LOWOCM] -force
eval [string range $original $cut end]
