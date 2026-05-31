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

-- check for spi protocol violations

entity SpiChecker is
   generic (
      POL_G : std_logic := '0';
      PHS_G : std_logic := '0'
   );
   port (
      clk   : in  std_logic;
      rst   : in  std_logic := '0';
      sclk  : in  std_logic;
      scsb  : in  std_logic;
      mosi  : in  std_logic;
      miso  : in  std_logic;
      viol  : out std_logic_vector(2 downto 0)
   );
end entity SpiChecker;

architecture rtl of SpiChecker is

   type RegType is record
      lsclk : std_logic;
      lscsb : std_logic;
      lmosi : std_logic;
      lmiso : std_logic;
      viol  : std_logic_vector(viol'range);
   end record RegType;

   constant REG_INIT_C : RegType := (
      lsclk => POL_G,
      lscsb => '1',
      lmosi => '0',
      lmiso => '0',
      viol  => (others => '0')
   );

   signal r    : RegType := REG_INIT_C;
   signal rin  : RegType := REG_INIT_C;
 
begin

   P_COMB : process (r, sclk, scsb, mosi, miso) is
      variable v : RegType;
   begin
      v          := r;
      v.lsclk    := sclk;
      v.lscsb    := scsb;
      v.lmiso    := miso;
      v.lmosi    := mosi;
      -- when scsb changes clock must be steady at POL
      if ( scsb /= r.lscsb ) then
         if ( sclk /= POL_G or r.lsclk /= POL_G ) then
            assert now < 1 ns report "SPI VIOLATION - clock not steady when CS changes" severity failure;
            v.viol(0) := '1';
         end if;
      end if;
      -- around active clock edge the data must be steady
      if ( sclk /= r.lsclk ) then 
         if ( sclk /= (POL_G xor PHS_G) ) then
            if ( mosi /= r.lmosi ) then
               assert now < 1 ns report "SPI VIOLATION - mosi not steady around active clock edge" severity failure;
               v.viol(1) := '1';
            end if;
            if ( miso /= r.lmiso ) then
               assert now < 1 ns report "SPI VIOLATION - miso not steady around active clock edge" severity warning;
               v.viol(2) := '1';
            end if;
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

   viol <= r.viol;
end architecture rtl;
