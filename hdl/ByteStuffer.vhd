library ieee;
use ieee.std_logic_1164.all;

entity ByteStuffer is
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
      sofInp  : in  std_logic;
      datOut  : out std_logic_vector(COMMA_G'range);
      vldOut  : out std_logic;
      rdyOut  : in  std_logic
   );
end entity ByteStuffer;

architecture rtl of ByteStuffer is

   -- special cases: 
   --   - message starts with comma -> , esc ,
   --   - message starts with ESC   -> , esc esc

   type StateType is (FWD, STUFF);

   type RegType is record
      dat     : std_logic_vector(COMMA_G'range);
      state   : StateType;
      sofSpc  : boolean; -- first byte is a comma or ESC
   end record RegType;

   constant REG_INIT_C : RegType := (
      dat     => (others => 'X'),
      state   => FWD,
      sofSpc  => false
   );

   signal r      : RegType := REG_INIT_C;
   signal rin    : RegType;

   signal isCom  : boolean;
   signal isEsc  : boolean;

begin

   assert COMMA_G'length = ESCAP_G'length severity failure;

   isCom <= (datInp = COMMA_G);
   isEsc <= (datInp = ESCAP_G);

   P_CMB : process (r, datInp, vldInp, isCom, isEsc, sofInp, rdyOut) is
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

      case ( r.state ) is
         when FWD   =>
            if ( (vldInp and rdyOut) = '1' ) then
               -- might have some work to do
               if ( (sofInp = '1') or isCom or isEsc ) then
                  -- hold data
                  v.dat   := datInp;
                  if ( (sofInp = '1') ) then
                     d       := COMMA_G;
                     if ( isCom or isEsc ) then
                        v.sofSpc  := true;
                     end if;
                  else
                     d       := ESCAP_G;
                  end if;
                  v.state := STUFF;
                  rdy     := '0';
                  vld     := '1';
               end if;
            end if;

         when STUFF =>
            vld := '1';
            d   := r.dat;
            if ( r.sofSpc ) then
               -- if the first octet was 'special' (ESC or comma) then
               -- we must escape it
               d := ESCAP_G;
            end if;
            if ( (vld and rdyOut) = '1' ) then
               if ( r.sofSpc ) then
                  rdy      := '0';
                  v.sofSpc := false;
               else
                  v.state := FWD;
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

end architecture rtl;
