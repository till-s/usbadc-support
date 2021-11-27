library ieee;
use     ieee.std_logic_1164.all;
use     ieee.math_real.all;

use     work.BasicPkg.all;

entity PipelinedRShifter is
   generic (
      DATW_G        : natural;
      AUXW_G        : natural := 0;
      SIGN_EXTEND_G : boolean := false;
      PIPL_SHIFT_G  : boolean := false
   );
   port (
      clk     : in  std_logic;
      rst     : in  std_logic;
      cen     : in  std_logic := '1';

      shift   : in  std_logic_vector(numBits(DATW_G - 1) - 1 downto 0);

      datInp  : in  std_logic_vector(DATW_G - 1 downto 0) := (others => '0');
      datOut  : out std_logic_vector(DATW_G - 1 downto 0);
      auxInp  : in  std_logic_vector(AUXW_G - 1 downto 0) := (others => '0');
      auxOut  : out std_logic_vector(AUXW_G - 1 downto 0)
   );
end entity PipelinedRShifter;

architecture rtl of PipelinedRShifter is

   constant LDW_C : natural := numBits( DATW_G - 1 );

   type RegArray is array (0 to LDW_C) of std_logic_vector(datInp'range);

   type AuxArray is array (0 to LDW_C) of std_logic_vector(auxInp'range);

   type ShfArray is array (1 to LDW_C) of std_logic_vector(shift'range);

   signal datReg : RegArray  := (others => (others => '0'));
   signal auxReg : AuxArray  := (others => (others => '0'));
   signal shfReg : ShfArray  := (others => (others => '0'));

begin

   datReg(LDW_C) <= datInp;
   auxReg(LDW_C) <= auxInp;
   shfReg(LDW_C) <= shift;

   GEN_NO_SHIFT_PIPELINED : if ( not PIPL_SHIFT_G ) generate
      GEN_SHIFT_STAGE : for stg in LDW_C - 1 downto shfReg'low generate
         shfReg(stg) <= shift;
      end generate GEN_SHIFT_STAGE;
   end generate GEN_NO_SHIFT_PIPELINED;

   GEN_SHIFT_PIPELINED : if ( PIPL_SHIFT_G ) generate
      GEN_SHIFT_STAGE : for stg in LDW_C - 1 downto shfReg'low generate
         P_SHF_PIPE : process ( clk ) is
         begin
            if ( rising_edge( clk ) ) then
               if ( rst = '1' ) then
                  shfReg(stg) <= (others => '0');
               elsif( cen = '1' ) then
                  shfReg(stg) <= shfReg(stg+1);
               end if;
            end if;
         end process P_SHF_PIPE;
      end generate GEN_SHIFT_STAGE;
   end generate GEN_SHIFT_PIPELINED;

   GEN_STAGE : for stg in LDW_C - 1 downto datReg'low generate
      signal sgn : std_logic_vector(2**stg - 1 downto 0);
   begin

      sgn <= (others => ite( SIGN_EXTEND_G, datReg(stg+1)(datReg(stg+1)'left), std_logic'('0') ) );

      P_SHFT : process ( clk ) is
      begin
         if ( rising_edge( clk ) ) then
            if ( rst = '1' ) then
               datReg(stg) <= (others => '0');
               auxReg(stg) <= (others => '0');
            elsif ( cen = '1' ) then
               if ( shfReg(stg+1)(stg) = '1' ) then
                  datReg(stg) <= sgn & datReg(stg+1)(datReg(stg)'left downto 2**stg);
               else
                  datReg(stg) <= datReg(stg+1);
               end if;
               auxReg(stg) <= auxReg(stg+1);
            end if;
         end if;
      end process P_SHFT;

   end generate GEN_STAGE;


   datOut <= datReg(0);
   auxOut <= auxReg(0);
end architecture rtl;

