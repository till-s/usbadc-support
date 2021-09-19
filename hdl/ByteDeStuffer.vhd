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
      dat     => (COMMA_G'high downto COMMA_G'low => 'X'),
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
      variable v    : RegType;
      variable rdyI : std_logic;
      variable vldO : std_logic;
      variable rstO : std_logic;
      variable lstO : std_logic;
   begin
      v   := r;

      -- combinatorial MUXes for rdyInp, vldOut, rstOut, lstOut
      rdyI := not r.vld;
      vldO := r.vld and vldInp;
      rstO := '0';
      lstO := '0';

      case ( r.state ) is
         when INIT  =>
            rdyI := '1';
            if ( vldInp = '1' ) then
               if ( not isEsc ) then
                  v.state := SYNC;
               end if;
            end if;

         when SYNC  =>
            rdyI := '1';
            if ( vldInp = '1' ) then
               if ( isEsc ) then
                  v.state  := INIT;
               elsif ( isCom ) then
                  v.state  := RUN;
                  v.synced := '1';
               end if;
            end if;

         when RUN   =>
            -- can consume data if register is empty or output is ready
            rdyI := ( not r.vld ) or rdyOut;
            if ( ( vldInp = '1' ) and isEsc ) then
               -- must not let them consume the data in the
               -- storage register
               vldO := '0';
            end if;
            if ( ( vldInp and rdyI ) = '1' ) then
               -- handle input data
               if ( isCom ) then
                  lstO  := '1';
                  -- if the data register is empty this means
                  -- there was an empty frame; assert reset
                  rstO  := not r.vld;
                  v.vld := '0';
               elsif ( isEsc ) then
                  v.state := ESC;
               else
                  -- consume and register new data
                  v.dat   := datInp;
                  v.vld   := '1';
               end if;
            end if;

         when ESC   =>
            rdyI := ( not r.vld ) or rdyOut;
            if ( ( vldInp = '1' ) and ( rdyI = '1' ) ) then
               v.vld   := '1';
               v.dat   := datInp;
               v.state := RUN;
            end if;
      end case;

      rdyInp <= rdyI;
      vldOut <= vldO;
      datOut <= r.dat;
      lstOut <= lstO;
      rstOut <= rstO;

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
