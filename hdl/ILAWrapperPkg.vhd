library ieee;
use     ieee.std_logic_1164.all;

package ILAWrapperPkg is

component ILAWrapper is
   port (
      clk      : in std_logic;
      trg0     : in std_logic_vector( 7 downto 0) := (others => '0');
      trg1     : in std_logic_vector( 7 downto 0) := (others => '0');
      trg2     : in std_logic_vector( 7 downto 0) := (others => '0');
      trg3     : in std_logic_vector( 7 downto 0) := (others => '0')
   );
end component ILAWrapper;

end package ILAWrapperPkg;
