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

use work.SimpleI2cMasterPkg.all;

entity SimpleI2cMasterTb is
end entity SimpleI2cMasterTb;

architecture rtl of SimpleI2cMasterTb is
   signal clk : std_logic := '0';
   signal rst : std_logic := '0';
   signal run : boolean   := true;

   signal req : I2cReqType := I2C_REQ_INIT_C;
   signal rdy : std_logic := '0';
   signal rdat: std_logic_vector(8 downto 0);
   signal don : std_logic;

   signal sclI: std_logic;
   signal sclO: std_logic;
   signal sdaI: std_logic;
   signal sdaO: std_logic;

   procedure sendCmd(
      signal   r     : inout I2cReqType;
      constant cmd   : std_logic_vector(1 downto 0) := I2C_CMD_NONE;
      constant start : std_logic := '0';
      constant stop  : std_logic := '0';
      constant wdata : std_logic_vector(7 downto 0) := (others => '1')
   ) is
   begin
      r.start <= start;
      r.stop  <= stop;
      r.cmd   <= cmd;
      r.wdat  <= wdata;
      r.vld   <= '1';
      while ( ( r.vld and rdy ) = '0' ) loop
         wait until rising_edge( clk );
      end loop;
      r.vld   <= '0';
      while ( don = '0' ) loop
         wait until rising_edge( clk );
      end loop;
   end procedure sendCmd;

begin

   P_CLK : process is
   begin
      if ( run ) then
         wait for 0.5 us;
         clk <= not clk;
      else
         wait;
      end if;
   end process P_CLK;

   P_TST : process is 
   begin
      sendCmd(r => req, start => '1');

      sendCmd(r => req, cmd => I2C_CMD_WRITE, wdata => x"A5");

      sendCmd(r => req, start => '1', cmd => I2C_CMD_READ);
      sendCmd(r => req, cmd => I2C_CMD_RNAK);

      sendCmd(r => req, stop => '1');

      run <= false;
      wait;
   end process P_TST;

   U_DUT : entity work.SimpleI2cMaster
      generic map (
         BUS_FREQ_HZ_G => 8.0E5
      )
      port map (
         clk  => clk,
         rst  => rst,
         req  => req,
         rdy  => rdy,
         rdat => rdat,
         don  => don,

         sclInp => sclI,
         sclOut => sclO,
         sdaInp => sdaI,
         sdaOut => sdaO
      );

   sdaI <= sdaO;
   sclI <= sclO;

end architecture rtl;
