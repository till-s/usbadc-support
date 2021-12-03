library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;
use ieee.math_real.all;

use work.CommandMuxPkg.all;

entity CommandWrapperSim is
end entity CommandWrapperSim;

architecture sim of CommandWrapperSim is

   constant MEM_DEPTH_C : natural := 1024;
   constant ADC_FIRST_C : unsigned(7 downto 0)   := x"A0";

   signal clk     : std_logic := '0';
   signal rst     : std_logic_vector(4 downto 0) := (others => '1');

   signal datIbo  : std_logic_vector(7 downto 0);
   signal vldIbo  : std_logic;
   signal rdyIbo  : std_logic;

   signal datObi  : std_logic_vector(7 downto 0);
   signal vldObi  : std_logic;
   signal rdyObi  : std_logic;

   signal run     : boolean      := true;

   signal bbi     : std_logic_vector(7 downto 0) := x"FF";
   signal bbo     : std_logic_vector(7 downto 0) := x"FF";

   signal adcDDR  : unsigned(7 downto 0)         := ADC_FIRST_C;
   signal adcA    : unsigned(7 downto 0)         := ADC_FIRST_C;
   signal adcB    : unsigned(7 downto 0)         := ADC_FIRST_C;
   signal dorA    : std_logic                    := '0';
   signal dorB    : std_logic                    := '0';
   signal dorDDR  : std_logic                    := '0';

begin

   P_CLK : process is
   begin
      if ( run ) then
         wait for 10 us;
         clk <= not clk;
      else
         wait;
      end if;
   end process P_CLK;

   U_DRV : entity work.SimPty
      port map (
         clk          => clk,

         vldOb        => vldIbo,
         datOb        => datIbo,
         rdyOb        => rdyIbo,

         vldIb        => vldObi,
         datIb        => datObi,
         rdyIb        => rdyObi
      );

   U_DUT : entity work.CommandWrapper
      generic map (
         FIFO_FREQ_G  => 4.0E5,
         MEM_DEPTH_G  => MEM_DEPTH_C
      )
      port map (
         clk          => clk,
         rst          => rst(rst'left),

         datIb        => datIbo,
         vldIb        => vldIbo,
         rdyIb        => rdyIbo,

         datOb        => datObi,
         vldOb        => vldObi,
         rdyOb        => rdyObi,

         bbo          => bbo,
         bbi          => bbi,

         adcClk       => clk,
         adcRst       => rst(rst'left),

         adcDataDDR(8 downto 1)  => std_logic_vector(adcDDR),
         adcDataDDR(         0)  => dorDDR,

         smplClk      => open
      );

   P_RST  : process ( clk ) is
   begin
      if ( rising_edge( clk ) ) then
         rst <= rst(rst'left - 1 downto 0) & '0';
      end if;
   end process P_RST;

   P_FILL_A : process ( clk ) is
   begin
      if ( rising_edge( clk ) ) then
         adcA <= adcA + 1;
      end if;
   end process P_FILL_A;

   P_FILL_B : process ( clk ) is
      variable i   : natural := 0;
      constant P_C : natural := 1000;
   begin
      if ( falling_edge( clk ) ) then
         adcB <= unsigned( to_signed( integer( round( 127.0 * sin(MATH_2_PI*real(i)/real(P_C)) ) ), adcB'length ) );
         i := i + 1;
         if ( i = P_C ) then
            i := 0;
         end if;
      end if;
   end process P_FILL_B;

   P_DDR : process( clk, adcA, adcB, dorA, dorB ) is
   begin
      if ( clk = '1' ) then
         adcDDR <= adcA;
         dorDDR <= dorA;
      else
         adcDDR <= adcB;
         dorDDR <= dorB;
      end if;
   end process P_DDR;


   P_BBMON : process (bbo) is
   begin
      report "BBO: " & std_logic'image(bbo(1)) & std_logic'image(bbo(0));
   end process P_BBMON;
   

end architecture sim;
