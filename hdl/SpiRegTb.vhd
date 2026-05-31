--LB-MIT
--
-- MIT License
--
-- Copyright (c) 2026 Till Straumann
--
-- Permission is hereby granted, free of charge, to any person obtaining a copy
-- of this software and associated documentation files (the "Software"), to deal
-- in the Software without restriction, including without limitation the rights
-- to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
-- copies of the Software, and to permit persons to whom the Software is
-- furnished to do so, subject to the following conditions:
--
-- The above copyright notice and this permission notice shall be included in all
-- copies or substantial portions of the Software.
--
-- THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
-- IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
-- FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
-- AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
-- LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
-- OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
-- SOFTWARE.
--
--LE-MIT

library ieee;
use     ieee.std_logic_1164.all;

-- a simple register with spi interface

entity SpiRegTb is
end entity SpiRegTb;

architecture rtl of SpiRegTb is

   signal clk  : std_logic := '0';

   signal sclk : std_logic := '0';
   signal scsb : std_logic := '1';
   signal sio  : std_logic;
   signal rs   : std_logic;
   signal ws   : std_logic;
   signal din  : std_logic_vector( 7 downto 0 );
   signal dou  : std_logic_vector( 7 downto 0 );

   signal run  : boolean := true;
   signal cnt  : natural := 0;
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

   din <= x"CA" when rs = '1' else (others => 'X');

   P_SEQ : process ( clk ) is
   begin
      if ( rising_edge( clk ) ) then
         if ( ws = '1' ) then
            assert dou = x"CA" report "data mismatch" severity failure;
            run <= false;
         end if;
         if ( scsb = '1' ) then
           if ( cnt = 0 ) then
              scsb <= '0';
           end if;
         else
           sclk <= not sclk;
           if ( sclk = '0' ) then
             if ( cnt = 8 ) then
                scsb <= '1';
                sclk <= '0';
             else
                cnt <= cnt + 1;
             end if;
           end if;
         end if;
      end if;
   end process P_SEQ;

   U_DUT1 : entity work.SpiReg
      port map (
         clk   => clk,

         sclk  => sclk,
         scsb  => scsb,
         mosi  => 'X',
         miso  => sio,

         data_inp => din,
         data_out => open,
         rs       => rs,
         ws       => open
      );

   U_DUT2 : entity work.SpiReg
      port map (
         clk   => clk,

         sclk  => sclk,
         scsb  => scsb,
         mosi  => sio,
         miso  => open,

         data_inp => (others => 'X'),
         data_out => dou,
         rs       => open,
         ws       => ws
      );

end architecture rtl;
