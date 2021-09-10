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

      busOb        : out SimpleBusMst;
      rdyOb        : in  std_logic;

      busMuxedIb   : out SimpleBusMstArray(NUM_CMDS_G - 1 downto 0);
      rdyMuxedIb   : in  std_logic_vector (NUM_CMDS_G - 1 downto 0);

      busMuxedOb   : in  SimpleBusMstArray(NUM_CMDS_G - 1 downto 0);
      rdyMuxedOb   : out std_logic_vector (NUM_CMDS_G - 1 downto 0)
   );
end entity CommandMux;

architecture rtl of CommandMux is

   subtype SelType   is natural range 0 to 2**NUM_CMD_BITS_C - 1;
   type    StateType is (IDLE, CMD, FWD, WAI);

   type RegType      is record
      state     : StateType;
      cmd       : SimpleBusMst;
      obLstSeen : boolean;
   end record RegType;

   constant REG_INIT_C : RegType := (
      state     => IDLE,
      cmd       => SIMPLE_BUS_MST_INIT_C,
      obLstSeen => false
   );

   signal     r   : RegType := REG_INIT_C;
   signal     rin : RegType;

begin
   P_COMB : process ( r, busIb, rdyMuxedIb, busMuxedOb, rdyOb ) is
      variable v   : RegType;
      variable sel : SelType;
      variable rdy : std_logic;
      variable ns  : StateType;
   begin

      v   := r;

      for i in busMuxedIb'range loop
         busMuxedIb(i).vld <= '0';
      end loop;

      rdy := '0';

      sel := to_integer(unsigned(r.cmd.dat(NUM_CMD_BITS_C - 1 downto 0)));

      -- drain unselected channels
      rdyMuxedOb <= (others => '1');
      busOb.vld  <= '0';

      case ( r.state ) is
         when IDLE =>
            rdy         := '1';
            v.obLstSeen := false;
            if ( busIb.vld = '1' ) then
               v.state := CMD;
               v.cmd   := busIb;
            end if;

         when CMD  =>

            if ( sel < NUM_CMDS_G ) then
               rdyMuxedOb(sel) <= rdyOb;
               busOb           <= busMuxedOb(sel);

               if ( (rdyOb and busMuxedOb(sel).vld and busMuxedOb(sel).lst) = '1' ) then
                  v.obLstSeen := true;
               end if;
            else
               v.obLstSeen := true;
            end if;

            if ( r.cmd.lst = '1' ) then
               if ( v.obLstSeen ) then
                  ns := IDLE;
               else
                  ns := WAI;
               end if;
            else
               ns := FWD;
            end if;

            if ( sel < NUM_CMDS_G ) then
               busMuxedIb(sel) <= r.cmd;
               -- we know 'vld' is asserted
               if ( rdyMuxedIb(sel) = '1' ) then
                  v.state := ns;
               end if;
            else
               v.state := ns; -- drop CMD
            end if;

         when FWD  =>
            if ( sel < NUM_CMDS_G ) then
               if ( not r.obLstSeen ) then
                  busMuxedIb(sel) <= busIb;
                  rdy             := rdyMuxedIb(sel);
               end if;

               rdyMuxedOb(sel) <= rdyOb;
               busOb           <= busMuxedOb(sel);

               if ( (rdyOb and busMuxedOb(sel).vld and busMuxedOb(sel).lst) = '1' ) then
                  v.obLstSeen := true;
               end if;
            else
               rdy         := '1'; -- drop CMD
               v.obLstSeen := true; -- pretend we've seen it
            end if;

            if ( (busIb.vld = '1') and (rdy = '1') and (busIb.lst = '1') ) then
               if ( v.obLstSeen ) then
                  v.state := IDLE;
               else 
                  v.state := WAI;
               end if;
            end if;

         when WAI => -- wait for outgoing frame to pass
            -- WAI can only be entered if sel < NUM_CMDS_G
            rdyMuxedOb(sel) <= rdyOb;
            busOb           <= busMuxedOb(sel);
            if ( (rdyOb and busMuxedOb(sel).vld and busMuxedOb(sel).lst) = '1' ) then
               v.state := IDLE;
            end if;
               
      end case;

      if ( r.obLstSeen and (sel < NUM_CMDS_G) ) then
         busOb.vld <= '0';
      end if;

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
