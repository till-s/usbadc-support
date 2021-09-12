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
      lstInp  : in  std_logic;
      datOut  : out std_logic_vector(COMMA_G'range);
      vldOut  : out std_logic;
      rdyOut  : in  std_logic
   );
end entity ByteStuffer;

architecture rtl of ByteStuffer is

   -- special cases: 
   --   - message starts with comma -> , esc ,
   --   - message starts with ESC   -> , esc esc

   type StateType is (FWD, STUFF, LST);

   type RegType is record
      dat     : std_logic_vector(COMMA_G'range);
      lst     : std_logic;
      state   : StateType;
   end record RegType;

   constant REG_INIT_C : RegType := (
      dat     => (COMMA_G'high downto COMMA_G'low   => 'X'),
      lst     => 'X',
      state   => FWD
   );

   signal r      : RegType := REG_INIT_C;
   signal rin    : RegType;

   signal isCom  : boolean;
   signal isEsc  : boolean;

begin

   assert COMMA_G'length = ESCAP_G'length severity failure;

   isCom <= (datInp = COMMA_G);
   isEsc <= (datInp = ESCAP_G);

   P_CMB : process (r, datInp, vldInp, isCom, isEsc, lstInp, rdyOut) is
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
               if ( isCom or isEsc ) then
                  -- hold data
                  v.dat   := datInp;
                  v.lst   := lstInp;
                  d       := ESCAP_G;
                  v.state := STUFF;
                  rdy     := '0';
                  vld     := '1';
               elsif ( lstInp = '1' ) then
                  v.state := LST;
                  rdy     := '0';
               end if;
            end if;

         when STUFF =>
            vld := '1';
            d   := r.dat;
            if ( (vld and rdyOut) = '1' ) then
               if ( ( r.lst = '1' ) ) then
                  v.state := LST;
                  rdy     := '0';
               else
                  v.state := FWD;
               end if;
            end if;

         when LST =>
            vld := '1';
            d   := COMMA_G;
            if ( (vld and rdyOut) = '1' ) then
               v.state := FWD;
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
