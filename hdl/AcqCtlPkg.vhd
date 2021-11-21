library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;
use ieee.math_real.all;

package AcqCtlPkg is

   type TriggerSrcType is (CHA, CHB, EXT);

   -- manual/immediate trigger is achieved by setting 'autoTimeMs' to 0

   type AcqCtlParmType is record
      src          : TriggerSrcType;
      rising       : boolean;
      lvl          : signed  (15 downto 0);
      nprets       : unsigned(15 downto 0);
      autoTimeMs   : unsigned(15 downto 0);
      decm         : unsigned(23 downto 0);
   end record AcqCtlParmType;

   function toSlv(constant x : in AcqCtlParmType) return std_logic_vector;

   function toAcqCtlParmType(constant x : std_logic_vector) return AcqCtlParmType;

   constant ACQ_CTL_PARM_INIT_C : AcqCtlParmType := (
      src         => CHA,
      lvl         => (others => '0'),
      rising      => true,
      nprets      => (others => '0'),
      autoTimeMs  => (others => '0'),
      decm        => (others => '0')
   );

   function acqCtlParmSizeBytes return natural;

end package AcqCtlPkg;

package body AcqCtlPkg is

   function acqCtlParmSizeBytes return natural is
      variable v : natural := 0;
      variable x : AcqCtlParmType;
   begin
      v := v + 1; -- src + 'rising' flag
      v := v + ( x.lvl'length + 7) / 8;
      v := v + ( x.nprets'length + 7) / 8;
      v := v + ( x.autoTimeMs'length + 7) / 8;
      v := v + ( x.decm'length + 7) / 8;
      return v;
   end function acqCtlParmSizeBytes;

   function toSlv(constant x : in AcqCtlParmType) return std_logic_vector is
      variable e : std_logic;
   begin
      if ( x.rising ) then
         e := '1';
      else
         e := '0';
      end if;
      return    std_logic_vector( x.decm       )
              & std_logic_vector( x.autoTimeMs )
              & std_logic_vector( x.nprets     )
              & std_logic_vector( x.lvl        )
              & x"0" & e & std_logic_vector( to_unsigned( TriggerSrcType'pos( x.src ), 3 ) );
   end function toSlv;

   function toAcqCtlParmType(constant x : std_logic_vector) return AcqCtlParmType is
      variable v : AcqCtlParmType := ACQ_CTL_PARM_INIT_C;
   begin
      if    ( x(2 downto 0) = "000" ) then
         v.src := CHA;
      elsif ( x(2 downto 0) = "001" ) then
         v.src := CHB;
      else
         v.src := EXT;
      end if;
      v.rising     := (x(3) = '1');
      v.lvl        :=   signed( x(23 downto  8) );
      v.nprets     := unsigned( x(39 downto 24) );
      v.autoTimeMs := unsigned( x(55 downto 40) );
      v.decm       := unsigned( x(69 downto 46) );
      return v;
   end function toAcqCtlParmType;
end package body AcqCtlPkg;
