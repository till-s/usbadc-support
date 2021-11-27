library ieee;
use     ieee.std_logic_1164.all;
use     ieee.math_real.all;

package BasicPkg is

   function numBits(constant x : integer) return integer;

end package BasicPkg;

package body BasicPkg is

   function numBits(constant x : integer) return integer is
   begin
      if ( x = 0 ) then return 1; end if;
      return integer( floor( log2( real( x ) ) ) ) + 1;
   end function numBits;

end package body BasicPkg;
