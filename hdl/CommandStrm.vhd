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
use     work.CommandMuxPkg.all;

-- splice a stream; adding the header/echo phase

entity CommandStrm is
   generic (
      SEL_DLY_G : natural := 0
   );
   port (
      clk       : in  std_logic;
      rst       : in  std_logic;

      strmMst   : in  SimpleBusMstType;
      strmRdy   : out std_logic;
      strmSel   : out std_logic;

      busIb     : in  SimpleBusMstType;
      rdyIb     : out std_logic;
      busOb     : out SimpleBusMstType;
      rdyOb     : in  std_logic
   );
end entity CommandStrm;

architecture rtl of CommandStrm is

   type StateType is (ECHO, FWD);

   type RegType is record
      state     : StateType;
      dly       : integer range -1 to SEL_DLY_G - 1;
   end record RegType;

   constant REG_INIT_C : RegType := (
      state     => ECHO,
      dly       => SEL_DLY_G - 1
  );

  signal r      : RegType := REG_INIT_C;
  signal rin    : RegType := REG_INIT_C;

begin

   P_COMB : process ( r, strmMst, busIb, rdyOb ) is
      variable v   : RegType;
   begin
      v   := r;
      -- default active in echo phase
      busOb     <= busIb;
      rdyIb     <= rdyOb;

      -- stop the stream
      strmRdy   <= '0';
      strmSel   <= '0';

      case ( r.state ) is
         when ECHO =>
            if ( busIb.vld = '1' ) then
               strmSel <= '1';
               if ( r.dly < 0 ) then
                  if ( ( rdyOb and busIb.lst ) = '1' ) then
                     if ( strmMst.vld = '1' ) then
                        -- forward only if there are data now
                        busOb.lst <= '0';
                        v.state   := FWD;
                     end if;
                     -- prepare for next time
                     v.dly := SEL_DLY_G - 1;
                  end if;
               else
                  -- hold off for 'dly' cycles
                  rdyIb     <= '0';
                  busOb.vld <= '0';
                  v.dly := r.dly - 1;
               end if;
            end if;

         when FWD =>
            busOb     <= strmMst;
            strmRdy   <= rdyOb;
            if ( ( strmMst.vld and rdyOb and strmMst.lst ) = '1' ) then
               v.state   := ECHO;
            end if;
      end case;
      rin <= v;
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

end architecture rtl;
