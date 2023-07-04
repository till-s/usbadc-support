library ieee;
use     ieee.std_logic_1164.all;

entity ILAWrapper is
   port (
      clk      : in std_logic;
      trg0     : in std_logic_vector( 7 downto 0) := (others => '0');
      trg1     : in std_logic_vector( 7 downto 0) := (others => '0');
      trg2     : in std_logic_vector( 7 downto 0) := (others => '0');
      trg3     : in std_logic_vector( 7 downto 0) := (others => '0')
   );
end entity ILAWrapper;

architecture rtl of ILAWrapper is

   component ila_1br is
      PORT (
         CLK     : IN STD_LOGIC;
         PROBE0  : IN STD_LOGIC_VECTOR(7 DOWNTO 0);
         PROBE1  : IN STD_LOGIC_VECTOR(7 DOWNTO 0);
         PROBE2  : IN STD_LOGIC_VECTOR(7 DOWNTO 0);
         PROBE3  : IN STD_LOGIC_VECTOR(7 DOWNTO 0)
      );
   end component ila_1br;

begin

   U_ILA : component ila_1br
      port map(
         CLK     => clk,
         PROBE0  => trg0,
         PROBE1  => trg1,
         PROBE2  => trg2,
         PROBE3  => trg3
      );

end architecture rtl;
