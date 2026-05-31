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

entity ILAWrapper is
   port (
      clk      : in std_logic;
      trg0     : in std_logic_vector( 7 downto 0) := (others => '0');
      trg1     : in std_logic_vector( 7 downto 0) := (others => '0');
      trg2     : in std_logic_vector( 7 downto 0) := (others => '0');
      trg3     : in std_logic_vector( 7 downto 0) := (others => '0')
   );
end entity ILAWrapper;

architecture rtl of ILAWrapper is

   component ila_1br is
      PORT (
         CLK     : IN STD_LOGIC;
         PROBE0  : IN STD_LOGIC_VECTOR(7 DOWNTO 0);
         PROBE1  : IN STD_LOGIC_VECTOR(7 DOWNTO 0);
         PROBE2  : IN STD_LOGIC_VECTOR(7 DOWNTO 0);
         PROBE3  : IN STD_LOGIC_VECTOR(7 DOWNTO 0)
      );
   end component ila_1br;

begin

   U_ILA : component ila_1br
      port map(
         CLK     => clk,
         PROBE0  => trg0,
         PROBE1  => trg1,
         PROBE2  => trg2,
         PROBE3  => trg3
      );

end architecture rtl;
