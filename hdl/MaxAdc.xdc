# set SCOPE_TO_REF to MaxADC in vivado for these constraints!

#set_max_delay -datapath_only -from [get_clocks -of_objects [get_ports busClk]]  -through [get_ports parms*] -to [get_clocks -of_objects [get_ports adcClk]] [expr min([join [get_property PERIOD [get_clocks -of_objects [get_ports adcClk]]] ,])]
set_max_delay -datapath_only -from [get_clocks -of_objects [get_ports busClk]]  -through [get_ports parms*] [expr min([join [get_property PERIOD [get_clocks -of_objects [get_ports adcClk]]] ,])]

#set_false_path -from [get_clocks -of_objects [get_ports adcClk]] -to [get_clocks -of_objects [get_ports busClk]] -through [get_nets *rWrCC*]
set_false_path -from [get_clocks -of_objects [get_ports adcClk]] -through [get_nets *rWrCC*]
