library ieee;
use ieee.std_logic_1164.all;

entity ByteDeStuffer is
   generic (
      -- length of the comma defines data width
      COMMA_G : std_logic_vector := x"CA";
      ESCAP_G : std_logic_vector := x"55"
   );
   port (
      clk     : in  std_logic;
      rst     : in  std_logic := '0';
      datInp  : in  std_logic_vector(COMMA_G'range);
      vldInp  : in  std_logic;
      rdyInp  : out std_logic;
      lstOut  : out std_logic;
      datOut  : out std_logic_vector(COMMA_G'range);
      vldOut  : out std_logic;
      rdyOut  : in  std_logic;
      synOut  : out std_logic;
      -- sequence of empty frames ,, asserts rstOut
      rstOut  : out std_logic
   );
end entity ByteDeStuffer;

architecture rtl of ByteDeStuffer is

   -- special cases:
   --   - message starts with comma -> , esc ,
   --   - message starts with ESC   -> , esc esc

   type StateType is (INIT, SYNC, RUN, ESC);

   type RegType is record
      state   : StateType;
      dat     : std_logic_vector(COMMA_G'range);
      vld     : std_logic;
      synced  : std_logic;
      lst     : std_logic;
   end record RegType;

   constant REG_INIT_C : RegType := (
      state   => INIT,
      dat     => (others => 'X'),
      vld     => '0',
      synced  => '0',
      lst     => '0'
   );

   signal r      : RegType := REG_INIT_C;
   signal rin    : RegType;

   signal isCom  : boolean;
   signal isEsc  : boolean;

begin

   assert COMMA_G'length = ESCAP_G'length severity failure;

   isCom <= (datInp = COMMA_G);
   isEsc <= (datInp = ESCAP_G);

   P_CMB : process (r, datInp, vldInp, isCom, isEsc, rdyOut) is
      variable v   : RegType;
      variable rdy : std_logic;
      variable lst : std_logic;
   begin
      v   := r;

      -- combinatorial MUX for rdyInp, lstOut
      rdy := not r.vld;
      lst := '0';

      case ( r.state ) is
         when INIT  =>
            if ( ( vldInp = '1' ) and ( rdy = '1' ) ) then
               if ( not isEsc ) then
                  v.state := SYNC;
               end if;
            end if;

         when SYNC  =>
            if ( ( vldInp = '1' ) and ( rdy = '1' ) ) then
               if ( isEsc ) then
                  v.state  := INIT;
               elsif ( isCom ) then
                  v.state  := RUN;
                  v.synced := '1';
               end if;
            end if;

         when RUN   =>
            if ( rdyOut = '1' ) then
               rdy   := '1';
               v.vld := '0';
            end if;
            if ( (vldInp and rdy) = '1' ) then
               if ( isEsc ) then
                  v.vld   := '0';
                  v.state := ESC;
               elsif ( isCom ) then
                  v.vld   := '0';
                  lst     := '1';
               else
                  v.vld   := '1';
                  v.dat   := datInp;
               end if;
               v.lst := lst;
            end if;

         when ESC   =>
            if ( ( vldInp = '1' ) and ( rdy = '1' ) ) then
               v.vld   := '1';
               v.dat   := datInp;
               v.state := RUN;
            end if;
      end case;

      rdyInp <= rdy;
      vldOut <= r.vld;
      datOut <= r.dat;
      lstOut <= lst;
      rstOut <= (lst and r.lst);

      rin    <= v;
   end process P_CMB;

   P_SEQ : process (clk) is
   begin
      if ( rising_edge( clk ) ) then
         if ( rst = '1' ) then
            r <= REG_INIT_C;
         else
            r <= rin;
         end if;
      end if;
   end process P_SEQ;

   synOut <= r.synced;

end architecture rtl;
