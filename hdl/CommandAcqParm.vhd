library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;
use ieee.math_real.all;

use work.CommandMuxPkg.all;
use work.AcqCtlPkg.all;

entity CommandAcqParm is
   generic (
      CLOCK_FREQ_G : real
   );
   port (
      clk          : in  std_logic;
      rst          : in  std_logic;
      
      mIb          : in  SimpleBusMstType;
      rIb          : out std_logic;

      mOb          : out SimpleBusMstType;
      rOb          : in  std_logic;

      parmsOb      : out AcqCtlParmType;
      trgOb        : out std_logic;
      ackIb        : in  std_logic
   );
end entity CommandAcqParm;

architecture rtl of CommandAcqParm is

   subtype  MaskType    is std_logic_vector(7 downto 0);

   constant M_GET_C     : Masktype := MaskType( to_unsigned(  0, MaskType'length ) );
   constant M_SET_SRC_BIT_C : natural  := 0;
   constant M_SET_EDG_BIT_C : natural  := 1;
   constant M_SET_LVL_BIT_C : natural  := 2;
   constant M_SET_NPT_BIT_C : natural  := 3;
   constant M_SET_AUT_BIT_C : natural  := 4;
   constant M_SET_DCM_BIT_C : natural  := 5;

   constant CMD_LEN_C   : natural := acqCtlParmSizeBytes + ( MaskType'length + 7 ) / 8 ;

   type MaskBitArray is array ( natural range 0 to CMD_LEN_C - 1 ) of natural range 0 to MaskType'length - 1;

   type StateType is (ECHO, MASK, PROC, TRIG);

   type RegType is record
      state         : StateType;
      mask          : MaskType;
      subCmd        : SubCommandAcqParmType;
      p             : AcqCtlParmType;
      count         : natural range 0 to CMD_LEN_C - 1;
      trg           : std_logic;
   end record RegType;

   constant REG_INIT_C : RegType := (
      state         => ECHO,
      mask          => M_GET_C,
      subCmd        => (others => '0'),
      p             => ACQ_CTL_PARM_INIT_C,
      count         => 0,
      trg           => '0'
   );

   signal r               : RegType := REG_INIT_C;
   signal rin             : RegType;

begin

   P_COMB : process ( r, mIb, rOb, ackIb ) is
      variable v       : RegType;
      variable rb      : std_logic_vector(7 downto 0);
   begin
      v := r;

      rb      := toSlv( r.p )( 7 + 8*r.count downto 8*r.count );

      mOb     <= mIb;

      rIb     <= rOb;

      case ( r.state ) is
         when ECHO =>
            if ( (rOb and mIb.vld) = '1' ) then
               v.subCmd := subCommandAcqParmGet( mIb.dat );
               v.state := MASK;
               v.count := 0;
            end if;

         when MASK  =>
            if ( (rOb and mIb.vld) = '1' ) then
               v.mask  := mIb.dat;
               mOb.dat <= r.mask;
               v.state := PROC;
            end if;

         when PROC  =>
            if ( ( rOb and mIb.vld ) = '1' ) then
               mOb.dat <= rb;

               rb(7 + 8*r.count downto 8*r.count) := mIb.dat;

               v.p := toAcqCtlParmType( rb );

               if ( r.mask( M_SET_SRC_BIT_C ) = '0' ) then
                  v.p.src        := r.p.src;
               end if;
               if ( r.mask( M_SET_EDG_BIT_C ) = '0' ) then
                  v.p.rising     := r.p.rising;
               end if;
               if ( r.mask( M_SET_LVL_BIT_C ) = '0' ) then
                  v.p.lvl        := r.p.lvl;
               end if;
               if ( r.mask( M_SET_NPT_BIT_C ) = '0' ) then
                  v.p.nprets     := r.p.nprets;
               end if;
               if ( r.mask( M_SET_AUT_BIT_C ) = '0' ) then
                  v.p.autoTimeMs := r.p.autoTimeMs;
               end if;
               if ( r.mask( M_SET_DCM_BIT_C ) = '0' ) then
                  v.p.decm       := r.p.decm;
               end if;
               if ( r.count = CMD_LEN_C - 1 ) then
                  if ( ( r.mask /= M_GET_C ) and ( mIb.lst = '1' ) ) then
                     v.trg    := not r.trg;
                     v.state  := TRIG;
                     -- hold off sending last byte until trigger is acked
                     mOb.vld  <= '0';
                  else
                     v.state  := ECHO;
                     mOb.lst  <= '1'; -- tell CommandMux we're done; they'll wait until 'mIb.lst' is seen
                  end if;
               else
                  v.count := r.count + 1;
               end if;
            end if;

         when TRIG =>
            mOb.vld <= '0';
            if ( ackIb = r.trg ) then
               mOb.vld <= '1';
               mOb.lst <= '1';
               mOb.dat <= std_logic_vector( r.p.decm(23 downto 16) );
               v.state := ECHO;
            end if;
      end case;

      -- check for early termination (short input)
      if ( ( v.state /= TRIG ) and ( rOb and mIb.vld and mIb.lst ) = '1' ) then
         v.state := ECHO;      
      end if;

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

   parmsOb <= r.p;
   trgOb   <= r.trg;

end architecture rtl;