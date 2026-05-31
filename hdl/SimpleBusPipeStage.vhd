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

use work.BasicPkg.all;
use work.CommandMuxPkg.all;

-- pipeline stage for SimpleBus to break combinatorial vld/rdy chains

entity SimpleBusPipeStage is
   port (
      clk          : in  std_logic;
      rst          : in  std_logic;
      busIb        : in  SimpleBusMstType;
      rdyIb        : out std_logic;

      busOb        : out SimpleBusMstType;
      rdyOb        : in  std_logic
   );
end entity SimpleBusPipeStage;

architecture rtl of SimpleBusPipeStage is
   type RegType is record
      buf          : SimpleBusMstType;
      ovfl         : SimpleBusMstType;
   end record RegType;

   constant REG_INIT_C : RegType := (
      buf          => SIMPLE_BUS_MST_INIT_C,
      ovfl         => SIMPLE_BUS_MST_INIT_C
   );

   signal r        : RegType := REG_INIT_C;
   signal rin      : RegType := REG_INIT_C;

begin

   P_COMB : process ( r, busIb, rdyOb ) is
      variable v : RegType;
   begin
      v          := r;
      if ( rdyOb = '1' ) then
         if ( r.buf.vld = '1' ) then
            if ( r.ovfl.vld = '1' ) then
               -- rdyIb is '0' here - we don't accept a new value
               -- but ship our 'buf' -> move overflow to buf
               v.buf      := r.ovfl;
               v.ovfl.vld := '0';
            else
               -- if busIb is valid then we consume and store
               -- otherwise v.buf.vld will become '0' and the
               -- data don't matter. 'ovfl.vld' will remain '0'
               v.buf      := busIb;
            end if;
         else
            -- assumption is that ovfl.vld cannot be '1' here
            v.buf := busIb;
         end if;
      else
         if ( r.buf.vld = '1' ) then
            if ( r.ovfl.vld = '0' ) then
               -- capture into overflow area, rdyIb will turn off
               -- should r.ovfl.vld assert to '1'
               v.ovfl := busIb;
            end if;
         else
            v.buf := busIb;
         end if;
      end if;

      rin        <= v;
   end process P_COMB;

   P_SEQ : process ( clk ) is
   begin
      if ( rising_edge( clk ) ) then
         if ( rst = '1' ) then
            r <= REG_INIT_C;
         else
            r <= rin;
         end if;
      end if;
   end process P_SEQ;

   rdyIb <= not (r.buf.vld and r.ovfl.vld);
   busOb <= r.buf;

end architecture rtl;
