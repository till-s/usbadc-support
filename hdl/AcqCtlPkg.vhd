library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;
use ieee.math_real.all;

package AcqCtlPkg is

   type TriggerSrcType is (CHA, CHB, EXT);

   -- manual/immediate trigger is achieved by setting 'autoTimeMs' to 0

   type AcqCtlParmType is record
      src          : TriggerSrcType;
      lvl          : signed  (15 downto 0);
      rising       : boolean;
      nprets       : unsigned(15 downto 0);
      autoTimeMs   : unsigned(15 downto 0);
   end record AcqCtlParmType;

   constant ACQ_CTL_PARM_INIT_C : AcqCtlParmType := (
      src         => CHA,
      lvl         => (others => '0'),
      rising      => true,
      nprets      => (others => '0'),
      autoTimeMs  => (others => '0')
   );

end package AcqCtlPkg;

package body AcqCtlPkg is

end package body AcqCtlPkg;
