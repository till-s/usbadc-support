library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

use work.CommandMuxPkg.all;

entity CommandWrapperTb is
end entity CommandWrapperTb;

architecture sim of CommandWrapperTb is

   constant MEM_DEPTH_C : natural := 16;
   constant ADC_FIRST_C : unsigned(7 downto 0) := x"A0";

   signal clk     : std_logic := '0';
   signal rst     : std_logic := '0';

   signal src     : SimpleBusMstType                         := SIMPLE_BUS_MST_INIT_C;
   signal snk     : SimpleBusMstType                         := SIMPLE_BUS_MST_INIT_C;
   signal srcR    : std_logic    := '0';
   signal snkR    : std_logic    := '0';

   signal datIbi  : std_logic_vector(7 downto 0);
   signal vldIbi  : std_logic;
   signal rdyIbi  : std_logic;
   signal datIbo  : std_logic_vector(7 downto 0);
   signal vldIbo  : std_logic;
   signal rdyIbo  : std_logic;

   signal datObi  : std_logic_vector(7 downto 0);
   signal vldObi  : std_logic;
   signal rdyObi  : std_logic;
   signal datObo  : std_logic_vector(7 downto 0);
   signal vldObo  : std_logic;
   signal rdyObo  : std_logic;

   signal run     : boolean      := true;

   signal bbi     : std_logic_vector(7 downto 0) := x"FF";
   signal bbo     : std_logic_vector(7 downto 0) := x"FF";

   signal adcDDR  : unsigned(7 downto 0) := ADC_FIRST_C;

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

   U_STUFF : entity work.ByteStuffer
      port map (
         clk          => clk,
         rst          => rst,

         datInp       => src.dat,
         vldInp       => src.vld,
         lstInp       => src.lst,
         rdyInp       => srcR,

         datOut       => datIbi,
         vldOut       => vldIbi,
         rdyOut       => rdyIbi
      );

   U_FIFOIb  : entity work.SFIFO
      port map (
         clk          => clk,
         rst          => rst,

         datIb        => datIbi,
         vldIb        => vldIbi,
         rdyIb        => rdyIbi,

         datOb        => datIbo,
         vldOb        => vldIbo,
         rdyOb        => rdyIbo
      );

   U_FIFOO   : entity work.SFIFO
      port map (
         clk          => clk,
         rst          => rst,

         datIb        => datObi,
         vldIb        => vldObi,
         rdyIb        => rdyObi,

         datOb        => datObo,
         vldOb        => vldObo,
         rdyOb        => rdyObo
      );


   U_DESTUFF : entity work.ByteDeStuffer
      port map (
         clk          => clk,
         rst          => rst,

         datInp       => datObo,
         vldInp       => vldObo,
         rdyInp       => rdyObo,

         datOut       => snk.dat,
         vldOut       => snk.vld,
         lstOut       => snk.lst,
         rdyOut       => snkR
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

         adcDataDDR(8 downto 1)   => std_logic_vector(adcDDR),
         adcDataDDR(         0)   => '0'

         smplClk      => open
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

   P_FEED : process is
      variable lst : std_logic;
   begin
      x(src, x"00", '1');
      x(src, x"00", '1');
      x(src, x"00", '1');
      x(src, x"11");
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

      
      x(src, x"00", '1');
      x(src, x"02", '1');

      for i in 1 to 500 loop
         wait until rising_edge( clk );
      end loop;
      run <= false;
      wait;
   end process P_FEED;

   BLK_RX : block is
         signal valid, ready, last : std_logic;
         signal data               : std_logic_vector(7 downto 0);
         signal lst                : std_logic := '1';
         signal foocnt             : integer   := -10;
   begin

      valid <= snk.vld;
      ready <= snkR;
      last  <= snk.lst;
      data  <= snk.dat;
      snkR  <= '1';

      P_REP : process ( clk ) is
      begin
         if ( rising_edge( clk ) ) then
           if ( (valid and ready) = '1') then
               report "Sink got " & integer'image(to_integer(unsigned(data))) & " lst: " & std_logic'image( last );

               if ( lst = '1' ) then
                  if (data(3 downto 0) = x"1") then
                     foocnt <= -1;
                  end if;
               end if;
               lst <= last;

               if ( foocnt >= -1 ) then
                  -- We'll never see the last value output on bbo back in bbi
                  if ( foocnt = bbFeed'high - 1 ) then
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
      end process P_REP;

   end block BLK_RX;

   bbi <= not bbo;

   P_BBMON : process (bbo) is
   begin
      report "BBO: " & std_logic'image(bbo(1)) & std_logic'image(bbo(0));
   end process P_BBMON;
   

end architecture sim;
