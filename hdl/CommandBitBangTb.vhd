library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

use work.CommandMuxPkg.all;

entity CommandBitBangTb is
end entity CommandBitBangTb;

architecture sim of CommandBitBangTb is

   constant N_CMDS_C : natural := 3;

   signal clk : std_logic := '0';
   signal rst : std_logic := '0';

   signal src : SimpleBusMst := SIMPLE_BUS_MST_INIT_C;
   signal snk : SimpleBusMst := SIMPLE_BUS_MST_INIT_C;
   signal srcR: std_logic    := '0';
   signal snkR: std_logic    := '1';

   signal bb  : std_logic_vector(7 downto 0);

   signal run : boolean      := true;

   procedure x(
      signal   mst : inout SimpleBusMst;
      constant dat : std_logic_vector(7 downto 0);
      constant lst : std_logic := '0'
   ) is
   begin
      mst.dat <= dat;
      mst.vld <= '1';
      mst.lst <= lst;
      wait until rising_edge( clk );
      while ( (mst.vld and srcR) = '0' ) loop
         wait until rising_edge( clk );
      end loop;
      mst.vld <= '0';
   end procedure x;

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

   P_SEQ : process ( clk ) is
   begin
      if ( rising_edge( clk ) ) then
      end if;
   end process P_SEQ;

   U_DUT : entity work.CommandBitBang
      generic map (
         CLOCK_FREQ_G => 400.0E3
      )
      port map (
         clk          => clk,
         rst          => rst,

         mIb          => src,
         rIb          => srcR,

         mOb          => snk,
         rOb          => snkR,

         bbi          => bb,
         bbo          => bb
      );

   P_FEED : process is
   begin
      x(src, x"F0");
      x(src, x"00");
      x(src, x"01", '1');
      x(src, x"F0", '1');
      x(src, x"F4");
      x(src, x"02");
      x(src, x"03");
      x(src, x"04");
      x(src, x"05", '1');
      x(src, x"F1", '1');
      x(src, x"F2");
      x(src, x"06", '1');
      x(src, x"F1");
      x(src, x"07");
      wait until rising_edge(clk);
      wait until rising_edge(clk);
      wait until rising_edge(clk);
      x(src, x"08");
      x(src, x"09", '1');
      x(src, x"F1");
      x(src, x"0A", '1');
      run <= false;
      wait;
   end process P_FEED;


   P_REP : process ( clk ) is
   begin
      if ( rising_edge( clk ) ) then
         if ( (snk.vld and snkR) = '1') then
            report "Channel got " & integer'image(to_integer(unsigned(snk.dat))) & " lst: " & std_logic'image( snk.lst );
         end if;
      end if;
   end process P_REP;


end architecture sim;
