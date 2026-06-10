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
use work.RegPkg.all;

-- Registers for common features that can be used across applications
package GenRegPkg is

   constant GEN_REG_VERSION_1_C : std_logic_vector(7 downto 0) := x"01";
   -- magic value to write to reconfiguration request register
   constant GEN_REG_RECONFIG_C  : std_logic_vector(7 downto 0) := x"3A";


   type GenRegOutType is record
      -- version 
      version         : std_logic_vector(7 downto 0);
      -- scratch bits initialized to 0 after configuration;
      -- can be used to indicate initialization status etc.
      -- not used by firmware
      scratch         : std_logic_vector(7 downto 0);
      -- up to 8 LEDs '1' to light up
      leds            : std_logic_vector(7 downto 0);
      -- request to reconfigure the FPGA
      reconfigure     : std_logic;
   end record GenRegOutType;

   constant GEN_REG_OUT_INIT_C : GenRegOutType := (
      version         => (others => '0'),
      scratch         => (others => '0'),
      leds            => (others => '0'),
      reconfigure     => '0'
   );

   type GenRegInpType is record
      ledsSupported   : std_logic_vector(7 downto 0);
      -- initial state of LEDs
      ledsInitial     : std_logic_vector(7 downto 0);
      -- indication of whether the FPGA supports reconfiguration
      reconfigurable  : std_logic;
   end record GenRegInpType;

   constant GEN_REG_INP_INIT_C : GenRegInpType := (
      ledsSupported   => (others => '0'),
      ledsInitial     => (others => '0'),
      reconfigurable  => '0'
   );

end package GenRegPkg;

package body GenRegPkg is
end package body GenRegPkg;
