library ieee;

use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity Ft240FifoTb is
end entity Ft240FifoTb;

architecture rtl of Ft240FifoTb is
   signal clk : std_logic := '0';
   signal rst : std_logic := '0';
   signal run : boolean   := true;

   signal datInp : std_logic_vector(7 downto 0) := x"54";

   signal rxe    : std_logic := '0';
   signal txf    : std_logic := '0';
   signal rrdy   : std_logic := '1';
   signal rvld   : std_logic := '1';

   signal data   : std_logic_vector(7 downto 0);

begin

   P_CLK : process is
   begin
      if ( run ) then
         wait for 0.5 us;
         clk <= not clk;
      else
         wait;
      end if;
   end process P_CLK;

   P_TST : process is 
      variable v : std_logic_vector(7 downto 0);
   begin

      for i in 1 to 20 loop
         wait until rising_edge( clk );
      end loop;

--      while ( (rrdy and rvld) = '0' ) loop
--         wait until rising_edge( clk );
--      end loop;
--
--      rrdy <= '0';
     
      for i in 1 to 20 loop
         wait until rising_edge( clk );
      end loop;


      run <= false;
      wait;
   end process P_TST;

   U_DUT : entity work.Ft240Fifo
      generic map (
         CLOCK_FREQ_G => 24.0E6
      )
      port map (
         clk     => clk,
         rst     => rst,

         fifoRXE => rxe,
         fifoRDT => datInp,
         fifoRDb => open,

         fifoTXF => txf,
         fifoWDT => open,
         fifoWR  => open,

         rdat    => data,
         rvld    => rvld,
         rrdy    => rrdy,

         wdat    => data,
         wvld    => rvld,
         wrdy    => rrdy
      );


end architecture rtl;
