library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;
use ieee.math_real.all;

package AcqCtlPkg is

   type TriggerSrcType is (CHA, CHB, EXT);

   -- manual/immediate trigger is achieved by setting 'autoTimeMs' to 0

   constant AUTO_TIME_STOP_C : unsigned(15 downto 0) := (others => '1');

   type AcqCtlParmType is record
      src          : TriggerSrcType;
      rising       : boolean;
      lvl          : signed  (15 downto 0);
      nprets       : unsigned(15 downto 0);
      autoTimeMs   : unsigned(15 downto 0);
      decm0        : unsigned( 3 downto 0);
      decm1        : unsigned(19 downto 0);
      shift0       : unsigned( 4 downto 0);
      shift1       : unsigned( 6 downto 0);
      scale        : signed  (17 downto 0);
   end record AcqCtlParmType;

   function toSlv(constant x : in AcqCtlParmType) return std_logic_vector;

   function toAcqCtlParmType(constant x : std_logic_vector) return AcqCtlParmType;

   constant ACQ_CTL_PARM_INIT_C : AcqCtlParmType := (
      src         => CHA,
      lvl         => (others => '0'),
      rising      => true,
      nprets      => (others => '0'),
      autoTimeMs  => AUTO_TIME_STOP_C,
      decm0       => (others => '0'),
      decm1       => (others => '0'),
      shift0      => (others => '0'),
      shift1      => (others => '0'),
      scale       => (16 => '1', others => '0')
   );

   function acqCtlParmSizeBytes return natural;

   function ite(constant x: in boolean) return std_logic;

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
      v := v + ( x.decm0'length  + x.decm1'length  + 7) / 8;
      v := v + ( x.shift0'length + x.shift1'length + x.scale'length + 7) / 8;
      return v;
   end function acqCtlParmSizeBytes;

   function ite(constant x: in boolean) return std_logic is
   begin
      if ( x ) then return '1'; else return '0'; end if;
   end function ite;

   function toSlv(constant x : in AcqCtlParmType) return std_logic_vector is
      constant e : std_logic        := ite( x.rising );
      constant v : std_logic_vector :=
             (
                std_logic_vector( x.shift0     )   --  5
              & std_logic_vector( x.shift1     )   --  7
              & "00" & std_logic_vector( x.scale ) -- 20
              & std_logic_vector( x.decm0      )
              & std_logic_vector( x.decm1      )
              & std_logic_vector( x.autoTimeMs )
              & std_logic_vector( x.nprets     )
              & std_logic_vector( x.lvl        )
              & x"0" & e & std_logic_vector( to_unsigned( TriggerSrcType'pos( x.src ), 3 ) ) );
      variable r : std_logic_vector(v'high downto v'low);
   begin
      r := v;
      return r;
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
report integer'image( x'left ) & " " & integer'image( x'right );
      v.lvl        :=   signed( x( 23 downto   8) );
      v.nprets     := unsigned( x( 39 downto  24) );
      v.autoTimeMs := unsigned( x( 55 downto  40) );
      v.decm1      := unsigned( x( 75 downto  56) );
      v.decm0      := unsigned( x( 79 downto  76) );
      v.scale      :=   signed( x( 97 downto  80) );
      v.shift1     := unsigned( x(106 downto 100) );
      v.shift0     := unsigned( x(111 downto 107) );
      return v;
   end function toAcqCtlParmType;
end package body AcqCtlPkg;
