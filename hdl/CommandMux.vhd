library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

use work.CommandMuxPkg.all;

entity CommandMux is
   generic (
      NUM_CMDS_G   : natural range 0 to NUM_CMD_MAX_C - 1
   );
   port (
      clk          : in  std_logic;
      rst          : in  std_logic;
      busIb        : in  SimpleBusMst;
      rdyIb        : out std_logic;

      busOb        : out SimpleBusMstArray(NUM_CMDS_G - 1 downto 0);
      rdyOb        : in  std_logic_vector (NUM_CMDS_G - 1 downto 0)
   );
end entity CommandMux;

architecture rtl of CommandMux is

   subtype SelType   is natural range 0 to 2**NUM_CMD_BITS_C - 1;
   type    StateType is (IDLE, CMD, FWD);

   type RegType      is record
      state : StateType;
      cmd   : SimpleBusMst;
   end record RegType;

   constant REG_INIT_C : RegType := (
      state => IDLE,
      cmd   => SIMPLE_BUS_MST_INIT_C
   );

   signal     r   : RegType := REG_INIT_C;
   signal     rin : RegType;

begin
   P_COMB : process ( r, busIb, rdyOb ) is
      variable v   : RegType;
      variable sel : SelType;
      variable rdy : std_logic;
      variable ns  : StateType;
   begin

      v   := r;

      for i in busOb'range loop
         busOb(i).vld <= '0';
      end loop;

      rdy := '0';

      sel := to_integer(unsigned(r.cmd.dat(NUM_CMD_BITS_C - 1 downto 0)));

      case ( r.state ) is
         when IDLE =>
            rdy := '1';
            if ( busIb.vld = '1' ) then
               v.state := CMD;
               v.cmd   := busIb;
            end if;

         when CMD  =>
            if ( r.cmd.lst = '1' ) then
               ns := IDLE;
            else
               ns := FWD;
            end if;
            if ( sel < NUM_CMDS_G ) then
               busOb(sel) <= r.cmd;
               -- we know 'vld' is asserted
               if ( rdyOb(sel) = '1' ) then
                  v.state := ns;
               end if;
            else
               v.state := ns; -- drop CMD
            end if;

         when FWD  =>
            if ( sel < NUM_CMDS_G ) then
               busOb(sel) <= busIb;
               rdy        := rdyOb(sel);
            else
               rdy        := '1'; -- drop CMD
            end if;
            if ( (busIb.vld = '1') and (rdy = '1') and (busIb.lst = '1') ) then
               v.state := IDLE;
            end if;
               
      end case;

      rdyIb  <= rdy;
      rin    <= v;

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
