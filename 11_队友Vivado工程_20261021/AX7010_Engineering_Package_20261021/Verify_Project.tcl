set root [file dirname [file normalize [info script]]]
open_project $root/AX7010_Sdoira_PL_V1/project/stereo_depth.xpr
puts "PACKAGE_PART=[get_property PART [current_project]]"
puts "PACKAGE_IP_REPOS=[get_property IP_REPO_PATHS [current_project]]"
set missing 0
foreach f [get_files -all] {
 if {![file exists $f]} {puts "PACKAGE_MISSING=$f"; incr missing}
}
if {$missing > 0} {error "Package has $missing missing files"}
report_ip_status -file $root/ip_status_20261021.rpt
open_bd_design [get_files design_1.bd]
validate_bd_design
puts PACKAGE_BD_VALIDATED
close_project
puts PACKAGE_OPEN_CHECK_PASS
exit
