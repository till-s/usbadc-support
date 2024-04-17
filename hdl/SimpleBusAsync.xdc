# set SCOPE_TO_REF to SimpleBusAsync in vivado for these constraints!

set_false_path -from [get_clocks -of_objects [get_ports clkIb]] -through [get_nets *busCC*]
