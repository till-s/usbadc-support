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

   signal ila_ctrl : std_logic_vector(35 downto 0);

   component ila_1br is
      PORT (
         CONTROL : INOUT STD_LOGIC_VECTOR(35 DOWNTO 0);
         CLK     : IN STD_LOGIC;
         TRIG0   : IN STD_LOGIC_VECTOR(7 DOWNTO 0);
         TRIG1   : IN STD_LOGIC_VECTOR(7 DOWNTO 0);
         TRIG2   : IN STD_LOGIC_VECTOR(7 DOWNTO 0);
         TRIG3   : IN STD_LOGIC_VECTOR(7 DOWNTO 0)
      );
   end component ila_1br;

   component chipscope_icon is
      PORT (
         control0 : out std_logic_vector(35 downto 0)
      );
   end component chipscope_icon;

begin

   U_ICON : component chipscope_icon
      port map (
         control0 => ila_ctrl
      );

   U_ILA : component ila_1br
      port map(
         CONTROL => ila_ctrl,
         CLK     => clk,
         TRIG0   => trg0,
         TRIG1   => trg1,
         TRIG2   => trg2,
         TRIG3   => trg3
      );

end architecture rtl;
