library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;
use ieee.math_real.all;

package CommandMuxPkg is

   type SimpleBusMstType is record
      vld : std_logic;
      lst : std_logic;
      dat : std_logic_vector(7 downto 0);
   end record SimpleBusMstType;

   constant SIMPLE_BUS_MST_INIT_C : SimpleBusMstType := (
      vld => '0',
      lst => 'X',
      dat => (others => 'X')
   );

   type SimpleBusMstArray is array (natural range <>) of SimpleBusMstType;

   constant NUM_CMD_BITS_C : natural := 4;
   -- command 0xF is reserved
   constant NUM_CMD_MAX_C  : natural := 2**NUM_CMD_BITS_C - 1;

   function numBits(constant x : integer) return integer;

end package CommandMuxPkg;

package body CommandMuxPkg is

   function numBits(constant x : integer) return integer is
   begin
      if ( x = 0 ) then return 1; end if;
      return integer( floor( log2( real( x ) ) ) ) + 1;
   end function numBits;

end package body CommandMuxPkg;
