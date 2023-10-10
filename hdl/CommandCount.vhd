library ieee;
use     ieee.std_logic_1164.all;
use     ieee.numeric_std.all;
use     ieee.math_real.all;

use     work.BasicPkg.all;
use     work.CommandMuxPkg.all;

-- simple counter; for testing/debugging stream output

entity CommandCount is
   port (
      clk       : in  std_logic;
      rst       : in  std_logic;

      busIb     : in  SimpleBusMstType;
      rdyIb     : out std_logic;
      busOb     : out SimpleBusMstType;
      rdyOb     : in  std_logic
   );
end entity CommandCount;

architecture rtl of CommandCount is

   subtype CountType is unsigned(32 downto 0);
   subtype IdxType   is natural range 0 to 3;

   type StateType is (ECHO, SZ, H1, H2, COUNT);

   type RegType is record
      state     : StateType;
      count     : CountType;
      idx       : IdxType;
   end record RegType;

   constant REG_INIT_C : RegType := (
      state     => ECHO,
      count     => (others => '1'),
      idx       => 0
  );

  signal r      : RegType := REG_INIT_C;
  signal rin    : RegType := REG_INIT_C;

begin

   P_COMB : process ( r, rin, busIb, rdyOb ) is
      variable v   : RegType;
   begin
      v   := r;
      busOb.vld <= '1';
      busOb.lst <= '0';
      busOb.dat <= std_logic_vector( r.count( 7 + 8*r.idx downto 8*r.idx ) );
      rdyIb     <= '0';

      case ( r.state ) is
         when ECHO =>
            busOb.dat <= busIb.dat;
            rdyIb     <= rdyOb;
            v.count(v.count'left) := '1';
            if ( ( busIb.vld and rdyOb ) = '1' ) then
               if ( busIb.lst = '1' ) then
                  v.state   := H1;
               else
                  v.state   := SZ;
               end if;
            end if;

         when SZ =>
            busOb.vld <= '0';
            rdyIb     <= '1';
            if ( busIb.vld = '1' ) then
               if ( ( busIb.lst = '1' ) and ( r.idx /= 3 ) ) then
                  v.count(v.count'left) := '1';
                  v.state               := H1;
               else
                  v.count( 7 + r.idx * 8 downto r.idx *8 ) := unsigned( busIb.dat );
                  v.idx := (r.idx + 1) mod 4;
                  if ( r.idx = 3 ) then
                     if ( busIb.lst = '1' ) then
                        v.state               := H1;
                        v.count(v.count'left) := '0';
                     else
                        -- this will keep latching incoming data into the counter MSB
                        v.idx   := r.idx;
                     end if;
                  end if;
               end if;
            end if;

         when H1 | H2 =>
            busOb.dat <= ( others => '0' );
            if ( rdyOb = '1' ) then
               if ( r.state = H1 ) then
                  v.state := H2;
               else
                  busOb.lst <= v.count(v.count'left);
                  if ( v.count(v.count'left) = '1' ) then
                     v.state := ECHO;
                  else
                     v.state := COUNT;
                  end if;
               end if;
            end if;

         when COUNT =>
           if ( rdyOb = '1' ) then
              v.idx := ( r.idx + 1 ) mod 4;
              if ( r.idx = 3 ) then
                 v.count := r.count - 1;
              end if;
              if ( v.count(v.count'left) = '1' ) then
                 v.state := ECHO;
              end if;
           end if; 
           busOb.lst <= v.count(v.count'left);
      end case;
      rin <= v;
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
