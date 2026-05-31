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

package SDRAMBufPkg is

   constant SDRAM_D_WIDTH_C : natural := 16;
   -- not all of these bits are used; depends
   -- on the actual device
   constant SDRAM_A_WIDTH_C : natural := 31;

   type SDRAMReqType is record
      req                : std_logic;
      rdnwr              : std_logic;
      wdat               : std_logic_vector(SDRAM_D_WIDTH_C - 1 downto 0);
      addr               : std_logic_vector(SDRAM_A_WIDTH_C - 1 downto 0);
   end record SDRAMReqType;

   constant SDRAM_REQ_INIT_C : SDRAMReqType := (
      req                => '0',
      rdnwr              => '0',
      wdat               => (others => '0'),
      addr               => (others => '0')
   );
 
   type SDRAMRepType is record
      -- request ack
      ack                : std_logic;
      -- read data valid (accounting for pipeline delay)
      vld                : std_logic;
      rdat               : std_logic_vector(SDRAM_D_WIDTH_C - 1 downto 0);
      -- ready for operation/init done
      rdy                : std_logic;
   end record SDRAMRepType;

   constant SDRAM_REP_INIT_C : SDRAMRepType := (
      ack                => '0',
      vld                => '0',
      rdat               => (others => '0'),
      rdy                => '0'
   );
  
end package SDRAMBufPkg;
