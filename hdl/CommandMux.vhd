library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

use work.CommandMuxPkg.all;
use work.ILAWrapperPkg.all;

entity CommandMux is
   generic (
      CMDS_SUPPORTED_G : CmdsSupportedType
   );
   port (
      clk          : in  std_logic;
      rst          : in  std_logic;
      busIb        : in  SimpleBusMstType;
      rdyIb        : out std_logic;

      busOb        : out SimpleBusMstType;
      rdyOb        : in  std_logic;

      busMuxedIb   : out SimpleBusMstArray(CMDS_SUPPORTED_G'length - 1 downto 0);
      rdyMuxedIb   : in  std_logic_vector (CMDS_SUPPORTED_G'length - 1 downto 0);

      busMuxedOb   : in  SimpleBusMstArray(CMDS_SUPPORTED_G'length - 1 downto 0);
      rdyMuxedOb   : out std_logic_vector (CMDS_SUPPORTED_G'length - 1 downto 0);

      abrt         : in  std_logic := '0';
      abrtDon      : out std_logic
   );
end entity CommandMux;

architecture rtl of CommandMux is

   constant NUM_CMDS_C : natural := CMDS_SUPPORTED_G'length;

   subtype SelType   is natural range 0 to 2**NUM_CMD_BITS_C - 1;
   type    StateType is (IDLE, CMD, FWD, WAI, ERR);

   type RegType      is record
      state     : StateType;
      cmd       : SimpleBusMstType;
      obLstSeen : boolean;
      abrtDon   : std_logic;
   end record RegType;

   constant REG_INIT_C : RegType := (
      state     => IDLE,
      cmd       => SIMPLE_BUS_MST_INIT_C,
      obLstSeen => false,
      abrtDon   => '0'
   );

   signal     r               : RegType := REG_INIT_C;
   signal     rin             : RegType;

   signal     rdyLoc          : std_logic;
   signal     busObLoc        : SimpleBusMstType;

   signal     t0              : std_logic_vector(7 downto 0) := (others => '0');
   signal     t1              : std_logic_vector(7 downto 0) := (others => '0');
   signal     t2              : std_logic_vector(7 downto 0) := (others => '0');
   signal     t3              : std_logic_vector(7 downto 0) := (others => '0');

begin

   t0    <= busIb.dat;

   t1(0) <= busIb.vld;
   t1(1) <= rdyLoc;
   t1(2) <= busIb.lst;

   process( r ) is
   begin
      if ( r.obLstSeen ) then
         t1(3) <= '1';
      else
         t1(3) <= '0';
      end if;
   end process;

   t1(5 downto 4) <= std_logic_vector(to_unsigned(StateType'pos(r.state), 2));

   t2    <= busObLoc.dat;

   t3(0) <= busObLoc.vld;
   t3(1) <= rdyOb;
   t3(2) <= busObLoc.lst;
   t3(6 downto 4) <= r.cmd.dat(2 downto 0);

   GEN_ILA : if ( false ) generate
   U_ILA : component ILAWrapper
      port map (
         clk  => clk,
         trg0 => t0,
         trg1 => t1,
         trg2 => t2,
         trg3 => t3
      );
   end generate;

   -- ise doesn't seem to handle nested records.
   -- (Got warnings about r.cmd missing from sensitivity list)
   P_COMB : process ( r, r.cmd, busIb, rdyMuxedIb, busMuxedOb, rdyOb, abrt ) is
      variable v     : RegType;
      variable sel   : SelType;
      variable selOK : boolean;
      variable rdy   : std_logic;
      variable ns    : StateType;
   begin

      v   := r;

      for i in busMuxedIb'range loop
         busMuxedIb(i) <= SIMPLE_BUS_MST_INIT_C;
      end loop;

      rdy          := '0';

      if ( r.abrtDon = '1' ) then
         v.abrtDon := '0';
      elsif ( r.state = IDLE ) then
         -- once we are back IDLE we ack the abort
         v.abrtDon := abrt;
      end if;

      sel          := to_integer(unsigned(r.cmd.dat(NUM_CMD_BITS_C - 1 downto 0)));
      selOK        := ( sel < NUM_CMDS_C ) and CMDS_SUPPORTED_G( sel ) ;

      -- drain unselected channels
      rdyMuxedOb   <= (others => '1');
      busObLoc     <= SIMPLE_BUS_MST_INIT_C;
      busObLoc.dat <= CMD_ERROR_C;
      busObLoc.lst <= '1';

      case ( r.state ) is
         when IDLE =>
            rdy         := '1';
            v.obLstSeen := false;
            if ( ( not abrt and busIb.vld ) = '1' ) then
               v.state     := CMD;
               v.cmd       := busIb;
            end if;

         when CMD  =>
            if ( abrt = '1' ) then
               -- nothing selected yet; can just go back
               v.state     := IDLE;
            else
               if ( selOK ) then
                  busObLoc        <= busMuxedOb(sel);
                  rdyMuxedOb(sel) <= rdyOb;

                  if ( (rdyOb and busMuxedOb(sel).vld and busMuxedOb(sel).lst) = '1' ) then
                     v.obLstSeen := true;
                  end if;
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

               if ( selOK ) then
                  busMuxedIb(sel) <= r.cmd;
                  -- we know 'vld' is asserted
                  if ( rdyMuxedIb(sel) = '1' ) then
                     v.state := ns;
                  end if;
               else
                  v.state := ERR;
               end if;
            end if;

         when ERR  =>
            if ( r.obLstSeen ) then
               busObLoc.vld <= '0';
            else
               busObLoc.vld <= '1';
               if ( rdyOb = '1' ) then -- rdy and vld
                  v.obLstSeen := true;
               end if;
            end if;
            -- r.cmd.lst could initially be '1' if they sent just the command byte
            if ( r.cmd.lst /= '1' ) then
               rdy := '1';
               if ( (busIb.vld and busIb.lst) = '1' ) then
                  -- abuse 'lst' flag in cmd
                  v.cmd.lst := '1';
               end if;
            elsif ( r.obLstSeen ) then
               -- input dumped and reply sent
               v.state := IDLE;
            end if;
            if ( abrt = '1' ) then
               -- nothing selected by mux, can just go back
               v.state := IDLE;
            end if;

         when FWD  =>
            busMuxedIb(sel) <= busIb;
            rdy             := rdyMuxedIb(sel);
            if ( abrt = '1' ) then
               busMuxedIb(sel).vld <= '1';
               busMuxedIb(sel).lst <= '1';
            end if;

            -- in FWD state we always forward inbound traffic (otherwise we'd be in WAI); however, we
            -- must stop outbound traffic after outbound 'lst' was seen
            if ( not r.obLstSeen ) then
               rdyMuxedOb(sel) <= (rdyOb or abrt);
               busObLoc        <= busMuxedOb(sel);

               if ( ((rdyOb or abrt) and busMuxedOb(sel).vld and busMuxedOb(sel).lst) = '1' ) then
                  v.obLstSeen := true;
               end if;
            end if;

            if ( ( rdy and ( (busIb.lst and busIb.vld) or abrt ) ) = '1' ) then
               if ( v.obLstSeen ) then
                  v.state := IDLE;
               else 
                  v.state := WAI;
               end if;
            end if;

         when WAI => -- wait for outgoing frame to pass
            -- WAI can only be entered if sel < NUM_CMDS_C aka selOK
            rdyMuxedOb(sel) <= rdyOb or abrt;
            busObLoc        <= busMuxedOb(sel);
            if ( (( abrt or rdyOb ) and busMuxedOb(sel).vld and busMuxedOb(sel).lst) = '1' ) then
               v.state   := IDLE;
            end if;
               
      end case;

      rdyLoc <= rdy;
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

   rdyIb   <= rdyLoc;
   busOb   <= busObLoc;
   abrtDon <= r.abrtDon;

end architecture rtl;
