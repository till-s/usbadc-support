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
      sofOut  : out std_logic;
      datOut  : out std_logic_vector(COMMA_G'range);
      vldOut  : out std_logic;
      rdyOut  : in  std_logic
   );
end entity ByteDeStuffer;

architecture rtl of ByteDeStuffer is

   -- special cases: 
   --   - message starts with comma -> , esc ,
   --   - message starts with ESC   -> , esc esc

   type StateType is (INIT, RUN);

   type RegType is record
      esc     : boolean;
      sof     : std_logic;
      dat     : std_logic_vector(COMMA_G'range);
      state   : StateType;
   end record RegType;

   constant REG_INIT_C : RegType := (
      esc     => false,
      sof     => '0',
      dat     => (others => 'X'),
      state   => INIT
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
      variable d   : std_logic_vector(datInp'range);
      variable vld : std_logic;
      variable rdy : std_logic;
   begin
      v   := r;

      -- combinatorial MUXes for rdyInp, vldOut, datOut
      rdy := rdyOut;
      vld := vldInp;
      d   := datInp;

      v.sof := '0';

      case ( r.state ) is
         when INIT  =>
            vld := '0';
            rdy := '1';
            if ( (vldInp = '1') ) then
               v.esc   := isEsc;
               v.state := RUN;
            end if;

         when RUN   =>
            if ( (vldInp and rdyOut) = '1' ) then
               v.esc   := isEsc;
               if ( isCom ) then
                  if ( not r.esc ) then
                     vld   := '0';
                     v.sof := '1';
                  end if;
               elsif ( isEsc ) then
                  if ( not r.esc ) then
                     vld   := '0';
                     v.sof := r.sof; -- restore across this escape
                  end if;
               end if;
            end if;
      end case;

      rdyInp <= rdy;
      vldOut <= vld;
      datOut <= d;
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

   sofOut <= r.sof;

end architecture rtl;
