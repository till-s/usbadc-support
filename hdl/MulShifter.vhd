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
      AUXV_WIDTH_G : natural := 0
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

   assert LD_PRE_DIV_C >= 0 report "invalid factor dimensiont" severity failure;

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

   prodOut <= resize( p, prodOut'length)
                 when ctl else
              shift_left( resize( p, prodOut'length ), LD_PRE_DIV_C );

   auxvOut <= aux( aux'left );

end architecture rtl;
