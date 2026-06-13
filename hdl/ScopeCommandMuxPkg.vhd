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

package ScopeCommandMuxPkg is

   -- Scope/Application registers
   constant CMD_APP_REGS_C    : std_logic_vector(7 downto 0) := x"03";
   -- Bit-bang serial peripherals (i2c, spi)
   constant CMD_BITBANG_C     : std_logic_vector(7 downto 0) := x"04";
   -- ADC memory
   constant CMD_ADC_MEMORY_C  : std_logic_vector(7 downto 0) := x"05";
   -- Scope parameters
   constant CMD_ACQ_PARAMS_C  : std_logic_vector(7 downto 0) := x"06";

   subtype  SubCommandAcqType is std_logic_vector(1 downto 0);
   constant CMD_ACQ_READ_C    : SubCommandAcqType := SubCommandAcqType( to_unsigned( 0, SubCommandAcqType'length ) );
   constant CMD_ACQ_FLUSH_C   : SubCommandAcqType := SubCommandAcqType( to_unsigned( 1, SubCommandAcqType'length ) );
   -- read back sample buffer size
   constant CMD_ACQ_MSIZE_C   : SubCommandAcqType := SubCommandAcqType( to_unsigned( 2, SubCommandAcqType'length ) );
   -- sample frequency
   constant CMD_ACQ_SFREQ_C   : SubCommandAcqType := SubCommandAcqType( to_unsigned( 3, SubCommandAcqType'length ) );

   function subCommandAcqGet(constant cmd : std_logic_vector(7 downto 0))
      return SubCommandAcqType;

   subtype SubCommandAcqParmType is std_logic_vector(1 downto 0);

   constant CMD_PRM_SET_GET   : SubCommandAcqParmType := SubCommandAcqParmType( to_unsigned( 0, SubCommandAcqParmType'length ) );

   function subCommandAcqParmGet(constant cmd : in std_logic_vector (7 downto 0))
      return SubCommandAcqParmType;

end package ScopeCommandMuxPkg;

package body ScopeCommandMuxPkg is

   function subCommandAcqGet(constant cmd : std_logic_vector(7 downto 0)) return SubCommandAcqType is
   begin
      return SubCommandAcqType( cmd(NUM_CMD_BITS_C + SubCommandAcqType'length - 1 downto NUM_CMD_BITS_C) );
   end function subCommandAcqGet;

   function subCommandAcqParmGet(constant cmd : in std_logic_vector (7 downto 0))
      return SubCommandAcqParmType is
   begin
      return SubCommandAcqParmType( cmd(NUM_CMD_BITS_C + SubCommandAcqParmType'length - 1 downto NUM_CMD_BITS_C) );
   end function subCommandAcqParmGet;

end package body ScopeCommandMuxPkg;
