library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

use work.CommandMuxPkg.all;

entity CommandMuxTb is
end entity CommandMuxTb;

architecture sim of CommandMuxTb is

   constant N_CMDS_C : natural := 4;

   signal clk : std_logic := '0';
   signal rst : std_logic := '0';

   signal mi  : SimpleBusMstArray(N_CMDS_C - 1 downto 0) := (others => SIMPLE_BUS_MST_INIT_C);
   signal mo  : SimpleBusMstArray(N_CMDS_C - 1 downto 0) := (others => SIMPLE_BUS_MST_INIT_C);
   signal ri  : std_logic_vector (N_CMDS_C - 1 downto 0) := (others => '1');
   signal ro  : std_logic_vector (N_CMDS_C - 1 downto 0) := (others => '1');

   signal src : SimpleBusMstType                         := SIMPLE_BUS_MST_INIT_C;
   signal snk : SimpleBusMstType                         := SIMPLE_BUS_MST_INIT_C;
   signal srcR: std_logic    := '0';
   signal snkR: std_logic    := '0';

   signal run : boolean      := true;

   signal bbi : std_logic_vector(7 downto 0) := x"FF";
   signal bbo : std_logic_vector(7 downto 0) := x"FF";

   procedure x(
      signal   mst : inout SimpleBusMstType;
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

   type Slv8Array is array (natural range <>) of std_logic_vector(7 downto 0);

   constant bbFeed : Slv8Array := (
       x"10",
       x"21",
       x"30",
       x"41",
       x"50",
       x"63",
       x"72",
       x"81",
       x"90"
   );

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
         rdyIb        => srcR,

         busOb        => snk,
         rdyOb        => snkR,

         busMuxedIb   => mi,
         rdyMuxedIb   => ri,

         busMuxedOb   => mo,
         rdyMuxedOb   => ro
      );

   U_BB : entity work.CommandBitBang
      generic map (
         CLOCK_FREQ_G => 4.0E5
      )
      port map (
         clk          => clk,
         rst          => rst,

         mIb          => mi(3),
         rIb          => ri(3),

         mOb          => mo(3),
         rOb          => ro(3),

         bbi          => bbi,
         bbo          => bbo
      );


   P_FEED : process is
      variable lst : std_logic;
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
      x(src, x"02");
      x(src, x"02", '1');
      x(src, x"01");
      x(src, x"02");
      wait until rising_edge(clk);
      wait until rising_edge(clk);
      wait until rising_edge(clk);
      x(src, x"03");
      x(src, x"04", '1');
      x(src, x"01");
      x(src, x"02", '1');

      x(src, x"13");
      for i in bbFeed'range loop
         if ( i = bbFeed'high ) then
            lst := '1';
         else
            lst := '0';
         end if;
         x(src, bbFeed(i), lst);
         if ( i mod 3 = 0 ) then
            wait until rising_edge( clk );
            wait until rising_edge( clk );
         end if;
      end loop;
      run <= false;
      wait;
   end process P_FEED;

   GEN_RX : for i in mi'low to mi'high + 1 generate
         signal valid, ready, last : std_logic;
         signal data               : std_logic_vector(7 downto 0);
         signal lst                : std_logic := '1';
         signal foocnt             : integer   := -10;
   begin

      G_M1 : if ( i <= mi'high ) generate
         valid <= mi(i).vld;
         ready <= ri(i);
         last  <= mi(i).lst;
         data  <= mi(i).dat;
      end generate G_M1;

      G_M2 : if ( i  > mi'high ) generate
         valid <= snk.vld;
         ready <= snkR;
         last  <= snk.lst;
         data  <= snk.dat;
         snkR  <= '1';
      end generate G_M2;

      P_REP : process ( clk ) is
      begin
         if ( rising_edge( clk ) ) then
            if ( (valid and ready) = '1') then
               report "Channel " & integer'image(i) & ": got " & integer'image(to_integer(unsigned(data))) & " lst: " & std_logic'image( last );

               if ( i > mi'high ) then
                  if ( lst = '1' ) then
                     if (data(3 downto 0) = x"3") then
                        foocnt <= -1;
                     end if;
                  end if;
                  lst <= last;

                  if ( foocnt >= -1 ) then
                     if ( foocnt = bbFeed'high ) then
                        foocnt <= -10;
                     else
                        foocnt <= foocnt + 1;
                     end if;

                     if ( foocnt >= 0 ) then
                        report integer'image(foocnt) &
                               " GOT " & integer'image(to_integer(unsigned(data))) &
                               " EXP " & integer'image(to_integer(unsigned(not bbFeed(foocnt)))) ;
                        assert( bbFeed(foocnt) = not data ) report "BBI mismatch" severity failure;
                     end if;
                  end if;
               end if;
            end if;
         end if;
      end process P_REP;

   end generate GEN_RX;

   mo(0) <= mi(0);
   ri(0) <= ro(0);

   mo(1).dat <= mi(1).dat;
   mo(1).vld <= '1';
   mo(1).lst <= '1';
   
   ri(1)     <= '1';

   ri(2)     <= '1';

   B_MO2 : block is
      signal dly : natural := 0;
   begin

   P_MO2 : process ( clk ) is
   begin
      if ( rising_edge( clk ) ) then
         mo(2).dat <= std_logic_vector(to_unsigned(dly, 8));
         if ( dly = 0 ) then
            mo(2).vld <= '0';
         else
            if (dly = 1) then
               mo(2).lst <= '1';
            end if;
            dly <= dly - 1;
         end if;
            
         if ( (mi(2).vld and ri(2)) = '1' ) then
            if ( mo(2).vld = '0' ) then
               mo(2).vld <= '1';
               mo(2).dat <= mi(2).dat;
               mo(2).lst <= '0';
               dly       <= 4;
            end if;
         end if;
      end if;
   end process P_MO2;

   end block B_MO2;

   bbi <= not bbo;

   P_BBMON : process (bbo) is
   begin
      report "BBO: " & std_logic'image(bbo(1)) & std_logic'image(bbo(0));
   end process P_BBMON;
   

end architecture sim;
