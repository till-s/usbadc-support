library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;
use ieee.math_real.all;

use work.BasicPkg.all;
use work.CommandMuxPkg.all;

entity SimpleBusAsyncTb is
end entity SimpleBusAsyncTb;

architecture sim of SimpleBusAsyncTb is
   signal clkIb : std_logic        := '0';
   signal busIb : SimpleBusMstType := SIMPLE_BUS_MST_INIT_C;
   signal rdyIb : std_logic;

   signal clkOb : std_logic        := '0';
   signal busOb : SimpleBusMstType;
   signal rdyOb : std_logic        := '0';

   signal run   : boolean          := true;

begin

   process is
   begin
      wait for 1 us;
      clkIb <= not clkIb;
      if not run then wait; end if;
   end process;

   process is
   begin
      wait for 1.0 us /30.0;
      clkOb <= not clkOb;
      if not run then wait; end if;
   end process;

   process (clkIb) is
      variable s1   : positive := 237;
      variable s2   : positive := 12901;
      variable rand : real;
   begin
      if ( rising_edge( clkIb ) ) then
         if ( busIb.vld = '1' ) then
            if ( rdyIb = '1' ) then
               busIb.dat <= std_logic_vector(unsigned(busIb.dat) + 1);
               if ( busIb.dat = x"FE" ) then
                  busIb.lst <= '1';
               end if;
               uniform(s1, s2, rand);
               if rand < 0.3 then
                  -- back-to-back cycle
               else
                  busIb.vld <= '0';
               end if;
            end if;
         else
            uniform(s1, s2, rand);
            if rand < 0.3 then
               busIb.vld <= '1';
            end if;
         end if;
      end if;
   end process;

   process (clkOb) is
      variable s1   : positive := 1291;
      variable s2   : positive := 237*13;
      variable rand : real;
      variable cmp  : std_logic_vector(7 downto 0) := (others => '0');
   begin
      if ( rising_edge( clkOb ) ) then
         if ( rdyOb = '1' ) then
            if ( busOb.vld = '1' ) then
               assert (cmp = busOb.dat)
                  report "Data Mismatch"
                  severity failure;
               assert ( (busOb.lst = '1') = (busOb.dat = x"ff") )
                  report "LST mismatch"
                  severity failure;
               cmp := std_logic_vector( unsigned(cmp) + 1 );
               uniform(s1, s2, rand);
               if rand < 0.3 then
                  -- back-to-back cycle
               else
                  rdyOb <= '0';
               end if;
               if ( busOb.lst = '1' ) then
                  run <= false;
                  report "TEST PASSED";
               end if;
            end if;
         else
            uniform(s1, s2, rand);
            if rand < 0.3 then
               rdyOb <= '1';
            end if;
         end if;
      end if;
   end process;


   U_DUT : entity work.SimpleBusAsync
      port map (
         clkIb  => clkIb,
         busIb  => busIb,
         rdyIb  => rdyIb,

         clkOb  => clkOb,
         busOb  => busOb,
         rdyOb  => rdyOb
      );
end architecture sim;
