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

-- compute
--
--  if ( ctl = '1' ) then
--    p = ( fwide * scl )
--  else
--    p = ( (fwide / 2**LD_PRE_DIV) * scl ) * 2**(LD_PRE_DIV)
-- 
-- NOTE: 'ctl' is currently not pipelined

entity MulShifter is
   generic (
      FACT_WIDTH_G : natural range 0 to 18 := 18;
      SCAL_WIDTH_G : natural range 0 to 18 := 18;
      PROD_WIDTH_G : natural range 0 to 36 := 36;
      FBIG_WIDTH_G : natural;
      AUXV_WIDTH_G : natural := 0;
      NO_POSTSHF_G : boolean := false
   );
   port (
      clk          : in  std_logic;
      rst          : in  std_logic;
      cen          : in  std_logic := '1';

      fbigInp      : in  signed(FBIG_WIDTH_G - 1 downto 0);
      scalInp      : in  signed(SCAL_WIDTH_G - 1 downto 0);
      auxvInp      : in  std_logic_vector(AUXV_WIDTH_G - 1 downto 0) := (others => '0');

      ctl          : in  boolean;

      auxvOut      : out std_logic_vector(AUXV_WIDTH_G - 1 downto 0) := (others => '0');
      prodOut      : out signed(SCAL_WIDTH_G + FBIG_WIDTH_G - 1 downto 0)
   );
end entity MulShifter;

architecture rtl of MulShifter is

   subtype  AuxVec           is std_logic_vector(AUXV_WIDTH_G - 1 downto 0);
   type     AuxArray         is array (natural range 1 downto 0) of AuxVec;
   subtype  ProdType         is signed(FACT_WIDTH_G + SCAL_WIDTH_G - 1 downto 0);

   subtype  FactType         is signed(FACT_WIDTH_G - 1 downto 0);
   signal   fw               : FactType              := (others => '0');
   signal   fs               : signed(scalInp'range) := (others => '0');
   signal   p                : ProdType              := (others => '0');
   signal   aux              : AuxArray              := (others => (others => '0'));

   constant LD_PRE_DIV_C     : natural               := FBIG_WIDTH_G - FACT_WIDTH_G;

begin

   assert LD_PRE_DIV_C >= 0 report "invalid factor dimension" severity failure;

   P_MUL : process ( clk ) is
   begin
      if ( rising_edge( clk ) ) then
         if    ( rst = '1' ) then
            fw  <= (others => '0');
            fs  <= (others => '0');
            p   <= (others => '0');
            aux <= (others => (others => '0'));
         elsif ( cen = '1' ) then

            aux <= aux(aux'left - 1 downto 0) & auxvInp;
            fs  <= scalInp;
            p   <= fw * fs; 

            if ( ctl ) then
               fw <= fbigInp(fw'range);
            else
               fw <= resize( shift_right( fbigInp, LD_PRE_DIV_C ), fw'length );
            end if;
         end if;
      end if;
   end process P_MUL;

   GEN_POSTSHIFT : if ( not NO_POSTSHF_G ) generate

   prodOut <= resize( p, prodOut'length)
                 when ctl else
              shift_left( resize( p, prodOut'length ), LD_PRE_DIV_C );

   end generate GEN_POSTSHIFT;

   GEN_NO_POSTSHIFT : if ( NO_POSTSHF_G ) generate

   prodOut <= resize( p, prodOut'length);

   end generate GEN_NO_POSTSHIFT;

   auxvOut <= aux( aux'left );

end architecture rtl;
