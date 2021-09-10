library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

package CommandMuxPkg is

   type SimpleBusMst is record
      vld : std_logic;
      lst : std_logic;
      dat : std_logic_vector(7 downto 0);
   end record SimpleBusMst;

   constant SIMPLE_BUS_MST_INIT_C : SimpleBusMst := (
      vld => '0',
      lst => 'X',
      dat => (others => 'X')
   );

   type SimpleBusMstArray is array (natural range <>) of SimpleBusMst;

   constant NUM_CMD_BITS_C : natural := 4;
   -- command 0xF is reserved
   constant NUM_CMD_MAX_C  : natural := 2**NUM_CMD_BITS_C - 1;

end package CommandMuxPkg;
