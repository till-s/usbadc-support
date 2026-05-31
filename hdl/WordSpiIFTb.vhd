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
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;
use ieee.math_real.all;

entity WordSpiIFTb is
end entity WordSpiIFTb;

architecture sim of WordSpiIFTb is
   signal clk  : std_logic := '0';
   signal rst  : std_logic := '0';
   signal fb   : std_logic;

   signal wdat : std_logic_vector(7 downto 0) := x"AD";
   signal wvld : std_logic                    := '0';
   signal wrdy : std_logic;

   signal rdat : std_logic_vector(7 downto 0);
   signal rvld : std_logic;
   signal rrdy : std_logic                    := '0';

   signal run  : boolean                      := true;

   signal stat : natural                      := 0; 
begin

   P_CLK : process is
   begin
      if ( run ) then
         wait for 100 ns;
         clk <= not clk;
      else
         wait;
      end if;
   end process P_CLK;

   P_TST : process ( clk ) is
   begin
      if ( rising_edge( clk ) ) then
         case ( stat ) is
            when 0      =>
               wvld <= '1';
               if ( (wvld and wrdy) = '1' ) then
                  wvld <= '0';
                  stat <= stat + 1;
               end if;
            when 1      =>
               rrdy <= '1';
               if ( (rvld and rrdy) = '1' ) then
                  rrdy <= '0';
                  assert rdat = wdat report "TEST FAILED: Readback mismatch" severity failure;
                  stat <= stat + 1;
               end if; 
            when others =>
               report "Test PASSED";
               run <= false;
         end case;
      end if;
   end process P_TST;

   U_DUT : entity work.WordSpiIF
      generic map (
         CLOCK_FREQ_G => 2.0E6
      )
      port map (
         clk          => clk,
         rst          => rst,

         rdat         => wdat,
         rvld         => wvld,
         rrdy         => wrdy,

         wdat         => rdat,
         wvld         => rvld,
         wrdy         => rrdy,

         sclk         => open,
         mosi         => fb,
         miso         => fb
      );
end architecture sim;
