library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

use work.CommandMuxPkg.all;

entity CommandWrapperSim is
end entity CommandWrapperSim;

architecture sim of CommandWrapperSim is

   constant MEM_DEPTH_C : natural := 16;
   constant ADC_FIRST_C : unsigned(7 downto 0) := x"A0";

   signal clk     : std_logic := '0';
   signal rst     : std_logic := '0';

   signal datIbo  : std_logic_vector(7 downto 0);
   signal vldIbo  : std_logic;
   signal rdyIbo  : std_logic;

   signal datObi  : std_logic_vector(7 downto 0);
   signal vldObi  : std_logic;
   signal rdyObi  : std_logic;

   signal run     : boolean      := true;

   signal bbi     : std_logic_vector(7 downto 0) := x"FF";
   signal bbo     : std_logic_vector(7 downto 0) := x"FF";

   signal adcDDR  : unsigned(7 downto 0) := ADC_FIRST_C;

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
         MEM_DEPTH_G  => MEM_DEPTH_C,
         SPI_SCLK_G   => 0,
         SPI_MOSI_G   => 1,
         SPI_MISO_G   => 2
      )
      port map (
         clk          => clk,
         rst          => rst,

         datIb        => datIbo,
         vldIb        => vldIbo,
         rdyIb        => rdyIbo,

         datOb        => datObi,
         vldOb        => vldObi,
         rdyOb        => rdyObi,

         bbo          => bbo,
         bbi          => bbi,

         adcClk       => clk,
         adcRst       => rst,

         adcDataDDR   => std_logic_vector(adcDDR)
      );

   P_FILL : process ( clk ) is
   begin
      if ( clk'event ) then
         if ( adcDDR < ADC_FIRST_C + 2*MEM_DEPTH_C ) then
            adcDDR <= adcDDR + 1;
         else
            adcDDR <= ADC_FIRST_C;
         end if;
      end if;
   end process P_FILL;

   P_BBMON : process (bbo) is
   begin
      report "BBO: " & std_logic'image(bbo(1)) & std_logic'image(bbo(0));
   end process P_BBMON;
   

end architecture sim;
