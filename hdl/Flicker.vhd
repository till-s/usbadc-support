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
use     ieee.numeric_std.all;
use     ieee.math_real.all;

use     work.BasicPkg.all;

-- make sure an LED marking activity stays off for some minimum time

entity Flicker is
   generic (
      CLOCK_FREQ_G     : real;             -- hz
      HOLD_TIME_G      : real      := 0.1; -- s
      ACTIVE_G         : std_logic := '1'
   );
   port (
      clk              : in  std_logic;
      rst              : in  std_logic := '0';
      datInp           : in  std_logic;
      datOut           : out std_logic
   );
end entity Flicker;

architecture rtl of Flicker is
   constant TICKS_C      : integer := integer( round( CLOCK_FREQ_G * HOLD_TIME_G ) ) - 2;
   constant WIDTH_C      : natural := numBits( TICKS_C );

   subtype  TimerType    is signed(WIDTH_C downto 0);

   constant TIMER_INIT_C : TimerType := (others => '1');

   signal   timer        : TimerType := TIMER_INIT_C;
   signal   ldin         : std_logic := not ACTIVE_G;
begin

   P_FLICK : process ( clk ) is
   begin
      if ( rising_edge( clk ) ) then
         if    ( rst = '1' ) then
            timer <= TIMER_INIT_C;
            ldin  <= not ACTIVE_G;
         else
            ldin  <= datInp;
            if    ( timer(timer'left) = '0' ) then
               timer <= timer - 1;
            elsif ( ( datInp = not ACTIVE_G ) and ( ldin = ACTIVE_G ) ) then
               timer <= to_signed( TICKS_C, timer'length );
            end if;
         end if;
      end if;
   end process P_FLICK;

   datOut <= datInp when timer(timer'left) = '1' else not ACTIVE_G;

end architecture rtl;
