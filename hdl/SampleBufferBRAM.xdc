# set SCOPE_TO_REF to SampleBuffer in vivado for these constraints!

set_false_path -from [get_clocks -of_objects [get_ports wrClk]] -through [get_nets *nsmplCC*]
set_false_path -from [get_clocks -of_objects [get_ports wrClk]] -through [get_nets *waddrCC*]
