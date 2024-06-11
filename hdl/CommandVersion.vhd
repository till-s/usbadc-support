library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;
use ieee.math_real.all;

use work.BasicPkg.all;
use work.CommandMuxPkg.all;

entity CommandVersion is
   generic (
      BOARD_VERSION_G : std_logic_vector( 7 downto 0);
      GIT_VERSION_G   : std_logic_vector(31 downto 0)
   );
   port (
      clk          : in  std_logic;
      rst          : in  std_logic;

      mIb          : in  SimpleBusMstType;
      rIb          : out std_logic;

      mOb          : out SimpleBusMstType;
      rOb          : in  std_logic
   );
end entity CommandVersion;

architecture rtl of CommandVersion is

   constant VERSION_C : Slv8Array := (
      0 => BOARD_VERSION_G,
      1 => CMD_API_VERSION_C,
      2 => GIT_VERSION_G(31 downto 24),
      3 => GIT_VERSION_G(23 downto 16),
      4 => GIT_VERSION_G(15 downto  8),
      5 => GIT_VERSION_G( 7 downto  0)
   );

   type StateType is (ECHO, DRAIN, PLAY);

   type RegType is record
      state         : StateType;
      addr          : integer range VERSION_C'low to VERSION_C'high;
   end record RegType;

   constant REG_INIT_C : RegType := (
      state         => ECHO,
      addr          => VERSION_C'low
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
            mOb.lst <= '0';
            v.addr  := VERSION_C'low;
            if ( (rOb and mIb.vld) = '1' ) then
               if ( mIb.lst /= '1' ) then
                  v.state := DRAIN;
               else
                  v.state := PLAY;
               end if;
            end if;

         when DRAIN =>
            mOb.vld <= '0';
            rIb     <= '1';
            if ( (mIb.vld and mIb.lst) = '1' ) then
               v.state   := PLAY;
            end if;

         when PLAY  =>
            rIb     <= '0';
            mOb.vld <= '1';
            mOb.lst <= '0';
            mOb.dat <= VERSION_C(r.addr);
            if ( rOb = '1' ) then
               if ( r.addr = VERSION_C'high ) then
                  v.state := ECHO;
                  mOb.lst <= '1';
               else
                  v.addr  := r.addr + 1;
               end if;
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
