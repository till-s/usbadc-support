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

entity CobsEchoTb is
end entity CobsEchoTb;

architecture sim of CobsEchoTb is
   signal clk         : std_logic := '0';

   signal ttyObVld    : std_logic;
   signal ttyObRdy    : std_logic;
   signal ttyObDat    : std_logic_vector(7 downto 0);

   signal ttyIbVld    : std_logic;
   signal ttyIbRdy    : std_logic;
   signal ttyIbDat    : std_logic_vector(7 downto 0);

   signal lpbckVld    : std_logic;
   signal lpbckRdy    : std_logic;
   signal lpbckLst    : std_logic;
   signal lpbckEof    : std_logic;
   signal lpbckDat    : std_logic_vector(7 downto 0);

   signal abrt        : std_logic;
   signal abrtAck     : std_logic := '1';
   signal syncReq     : std_logic := '0';
   signal syncAck     : std_logic;

   signal run         : boolean := true;

begin

   P_CLK : process is
   begin
      if ( run ) then
         wait for 10 ns;
         clk <= not clk;
      else
         wait;
      end if;
   end process P_CLK;

   U_DRV : entity work.SimPty
      port map (
         clk          => clk,

         vldOb        => ttyObVld,
         datOb        => ttyObDat,
         rdyOb        => ttyObRdy,

         vldIb        => ttyIbVld,
         datIb        => ttyIbDat,
         rdyIb        => ttyIbRdy,

         abrt         => abrt,
         abrtDon      => abrtAck
      );

   U_DEC : entity work.COBSDecoder
      port map (
         clk          => clk,
         rst          => '0',

         datOut       => lpbckDat,
         vldOut       => lpbckVld,
         lstOut       => lpbckLst,
         rdyOut       => lpbckRdy,

         datInp       => ttyObDat,
         vldInp       => ttyObVld,
         rdyInp       => ttyObRdy
      );

   U_ENC : entity work.COBSEncoder
      port map (
         clk          => clk,
         rst          => '0',

         datInp       => lpbckDat,
         vldInp       => lpbckVld,
         lstInp       => lpbckLst,
         rdyInp       => lpbckRdy,

         -- emit a frame delimiter (waits until internal state is idle);
         -- this does not interrupt a frame. Useful to sync receivers
         -- when otherwise nothing is going on.
         synReq       => syncReq,
         -- 1-cycle pulse to acknowledge the SYNC was sent. User
         -- should withdraw the synReq after synAck - otherwise
         -- a flood of EOFs may be caused.
         synAck       => syncAck,

         datOut       => ttyIbDat,
         vldOut       => ttyIbVld,
         rdyOut       => ttyIbRdy
      );

end architecture sim;
