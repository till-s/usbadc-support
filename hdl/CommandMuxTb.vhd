library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

use work.CommandMuxPkg.all;

entity CommandMuxTb is
end entity CommandMuxTb;

architecture sim of CommandMuxTb is

   constant N_CMDS_C : natural := 2;

   signal clk : std_logic := '0';
   signal rst : std_logic := '0';

   signal m   : SimpleBusMstArray(N_CMDS_C - 1 downto 0) := (others => SIMPLE_BUS_MST_INIT_C);
   signal r   : std_logic_vector (N_CMDS_C - 1 downto 0) := (others => '1');

   signal src : SimpleBusMst := SIMPLE_BUS_MST_INIT_C;
   signal rdy : std_logic    := '0';

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
      while ( (mst.vld and rdy) = '0' ) loop
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

   U_DUT : entity work.CommandMux
      generic map (
         NUM_CMDS_G   => N_CMDS_C
      )
      port map (
         clk          => clk,
         rst          => rst,
         busIb        => src,
         rdyIb        => rdy,

         busOb        => m,
         rdyOb        => r
      );

   P_FEED : process is
   begin
      x(src, x"00");
      x(src, x"01");
      x(src, x"02", '1');
      x(src, x"00", '1');
      x(src, x"04");
      x(src, x"00");
      x(src, x"00");
      x(src, x"00");
      x(src, x"00", '1');
      x(src, x"01", '1');
      x(src, x"01");
      x(src, x"02");
      wait until rising_edge(clk);
      wait until rising_edge(clk);
      wait until rising_edge(clk);
      x(src, x"03");
      x(src, x"04", '1');
      run <= false;
      wait;
   end process P_FEED;

   GEN_RX : for i in m'range generate

      P_REP : process ( clk ) is
      begin
         if ( rising_edge( clk ) ) then
            r(i) <= '1';
            if ( (m(i).vld = '1') and (r(i) = '1') ) then
               report "Channel " & integer'image(i) & ": got " & integer'image(to_integer(unsigned(m(i).dat))) & " lst: " & std_logic'image( m(i).lst );
               if (i = 0) then
                  r(i) <= '0';
               end if;
            end if;
         end if;
      end process P_REP;

   end generate GEN_RX;

end architecture sim;
