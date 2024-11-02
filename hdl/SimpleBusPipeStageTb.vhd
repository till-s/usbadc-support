library ieee;

use ieee.std_logic_1164.all;
use ieee.numeric_std.all;
use ieee.math_real.all;

use work.BasicPkg.all;
use work.CommandMuxPkg.all;

entity SimpleBusPipeStageTb is
end entity SimpleBusPipeStageTb;

architecture Sim of SimpleBusPipeStageTb is
   constant  CLK_HALF_PER_C   : time             := 5 ns;
   constant  DRV_L_C          : natural          := 7;
   constant  RCV_L_C          : natural          := 5;
   constant  XXX_C            : std_logic_vector(7 downto 0) := (others => 'X');

   signal    clk              : std_logic        := '0';
   signal    run              : boolean          := true;
   signal    busIb            : SimpleBusMstType := SIMPLE_BUS_MST_INIT_C;
   signal    busOb            : SimpleBusMstType := SIMPLE_BUS_MST_INIT_C;
   signal    rdyIb            : std_logic;
   signal    rdyOb            : std_logic        := '0';

   signal    drvDat           : unsigned(7 downto 0) := (others => '0');
   signal    rcvDat           : unsigned(7 downto 0) := (others => '0');
   signal    drv              : unsigned(DRV_L_C - 1 downto 0) := (others => '0');
   signal    rcv              : unsigned(RCV_L_C - 1 downto 0) := (others => '0');
   signal    drvCnt           : natural := 0;
   signal    rcvCnt           : natural := 0;
   signal    loopCnt          : natural := 0;
begin

   P_CLK : process is
   begin
      if (not run) then wait; end if;
      wait for CLK_HALF_PER_C;
      clk <= not clk;
   end process P_CLK;

   P_DRV : process ( clk ) is
      variable dat : unsigned(7 downto 0);
   begin
      if ( rising_edge( clk ) ) then
         dat := drvDat;
         if ( ( busIb.vld and rdyIb ) = '1' ) then
            dat    := dat + 1;
            drvDat <= drvDat + 1;
         end if;
         busIb.vld <= drv(drvCnt);
         if ( drv(drvCnt) = '1' ) then
            busIb.dat <= std_logic_vector(dat);
         else
            busIb.dat <= XXX_C;
         end if;
         if ( drvCnt = DRV_L_C - 1 ) then
            drvCnt  <= 0;
            drv     <= drv + 1;
            loopCnt <= loopCnt + 1;
            -- already divided by DRV_L_C
            if ( loopCnt = 2**DRV_L_C * RCV_L_C * 2**RCV_L_C  ) then
               busIb.lst <= '1';
            end if;
         else
            drvCnt <= drvCnt + 1;
         end if;
      end if;
   end process P_DRV;

   P_RCV : process ( clk ) is
      variable cmp : natural := 0;
   begin
      if ( rising_edge( clk ) ) then
         if ( (rdyOb and busOb.vld) = '1' ) then
            assert std_logic_vector(rcvDat) = busOb.dat report "data mismatch" severity failure;
            cmp := cmp + 1;
            rcvDat <= rcvDat + 1;
            if ( busOb.lst = '1' ) then
               report "Test PASSED; " & integer'image(cmp) & " comparisons";
               run <= false;
            end if;
         end if;
         rdyOb <= rcv(rcvCnt);
         if ( rcvCnt = RCV_L_C - 1 ) then
            rcvCnt <= 0;
            rcv    <= rcv + 1;
         else
            rcvCnt <= rcvCnt + 1;
         end if;
      end if;
   end process P_RCV;


   U_DUT : entity work.SimpleBusPipeStage
      port map (
         clk      => clk,
         rst      => '0',
         busIb    => busIb,
         rdyIb    => rdyIb,
         busOb    => busOb,
         rdyOb    => rdyOb
      );

end architecture Sim;
