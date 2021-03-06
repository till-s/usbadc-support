library ieee;
use     ieee.std_logic_1164.all;
use     ieee.math_real.all;

package BasicPkg is

   function numBits(constant x : in integer) return integer;

   function toSl(constant x : in boolean) return std_logic;

   function ite(constant x : in boolean; constant a,b: in std_logic) return std_logic;

end package BasicPkg;

package body BasicPkg is

   function numBits(constant x : in integer) return integer is
   begin
      if ( x = 0 ) then return 1; end if;
      return integer( floor( log2( real( x ) ) ) ) + 1;
   end function numBits;

   function toSl(constant x : in boolean) return std_logic is
   begin
      if ( x ) then return '1'; else return '0'; end if;
   end function toSl;

   function ite(constant x : in boolean; constant a,b: in std_logic) return std_logic is
   begin
      if ( x ) then return a; else return b; end if;
   end function ite;
end package body BasicPkg;
