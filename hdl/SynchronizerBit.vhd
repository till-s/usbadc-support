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

entity SynchronizerBit is
   generic (
      STAGES_G : positive  := 2;
      WIDTH_G  : positive  := 1;
      IN_REG_G : boolean   := false;
      RSTPOL_G : std_logic := '0'
   );
   port (
      clk      : in  std_logic;
      rst      : in  std_logic;
      datInp   : in  std_logic_vector(WIDTH_G - 1 downto 0);
      datOut   : out std_logic_vector(WIDTH_G - 1 downto 0);
      clkInp   : in  std_logic := '0';
      rstInp   : in  std_logic := '0'
   );
end entity SynchronizerBit;

architecture rtl of SynchronizerBit is

   attribute syn_srlstyle  : string;
   attribute shreg_extract : string;
   attribute ASYNC_REG     : string;

   signal    datInpLoc     : std_logic_vector(datInp'range);

begin

   GEN_SYNC : for i in datInp'range generate

      signal syncReg : std_logic_vector(STAGES_G - 1 downto 0) := (others => RSTPOL_G);

      attribute syn_srlstyle  of syncReg : signal is "registers";

      attribute shreg_extract of syncReg : signal is "no";

      attribute ASYNC_REG     of syncReg : signal is "TRUE";

   begin
      P_SYNC : process ( clk ) is
      begin
         if ( rising_edge( clk ) ) then
            if ( rst = '1' ) then
               syncReg <= (others => RSTPOL_G);
            else
               syncReg <= syncReg(syncReg'left - 1 downto syncReg'right) & datInpLoc(i);
            end if;
         end if;
      end process P_SYNC;

      datOut(i) <= syncReg(syncReg'left);

   end generate GEN_SYNC;

   GEN_IN_REG : if ( IN_REG_G ) generate

      P_REG : process ( clkInp ) is
      begin
         if ( rising_edge( clkInp ) ) then
            if ( rstInp = '1' ) then
               datInpLoc <= (others => RSTPOL_G);
            else
               datInpLoc <= datInp;
            end if;
         end if;
      end process P_REG;

   end generate GEN_IN_REG;

   GEN_NO_REG : if ( not IN_REG_G ) generate
      datInpLoc <= datInp;
   end generate GEN_NO_REG;

end architecture rtl;



