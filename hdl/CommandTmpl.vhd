library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;
use ieee.math_real.all;

use work.CommandMuxPkg.all;

entity CommandXXX is
   generic (
      CLOCK_FREQ_G : real
   );
   port (
      clk          : in  std_logic;
      rst          : in  std_logic;
      
      mIb          : in  SimpleBusMstType;
      rIb          : out std_logic;

      mOb          : out SimpleBusMstType;
      rOb          : in  std_logic
   );
end entity CommandXXX;

architecture rtl of CommandXXX is

   -- the following shoudl go into CommandMuxPkg.vhd
   subtype SubCommandXXXType is std_logic_vector(1 downto 0);

   function subCommandXXXGet(constant cmd : in std_logic_vector (7 downto 0))
      return SubCommandXXXType is
   begin
      return SubCommandXXXType( cmd(NUM_CMD_BITS_C + SubCommandXXXType'length - 1 downto NUM_CMD_BITS_C) );
   end function subCommandXXXGet;

   type StateType is (ECHO, FWD);

   type RegType is record
      state         : StateType;
      subCmd        : SubCommandXXXType;
   end record RegType;

   constant REG_INIT_C : RegType := (
      state         => ECHO,
      subCmd        => (others => '0')
   );

   signal r               : RegType := REG_INIT_C;
   signal rin             : RegType;

begin

   P_COMB : process ( r, mIb, rOb ) is
      variable v       : RegType;
   begin
      v := r;

      mOb     <= mIb;

      rIb     <= rOb;

      case ( r.state ) is
         when ECHO =>
            if ( (rOb and mIb.vld) = '1' ) then
               v.subCmd := subCommandXXXGet( mIb.dat );
               if ( mIb.lst /= '1' ) then
                  v.state := FWD;
               end if;
            end if;

         when FWD  =>
            if ( ( rOb and mIb.vld and mIb.lst ) = '1' ) then
               v.state   := ECHO;
            end if;
      end case;

      rin     <= v;
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

end architecture rtl;
