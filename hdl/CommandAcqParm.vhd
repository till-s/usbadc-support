library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;
use ieee.math_real.all;

use work.CommandMuxPkg.all;
use work.AcqCtlPkg.all;

entity CommandAcqParm is
   generic (
      CLOCK_FREQ_G : real;
      MEM_DEPTH_G  : natural
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
      ackIb        : in  std_logic;
      parmsRdyIb   : in  std_logic := '1'
   );
end entity CommandAcqParm;

architecture rtl of CommandAcqParm is

   subtype  MaskType    is std_logic_vector(31 downto 0);

   constant M_GET_C     : Masktype := MaskType( to_unsigned(  0, MaskType'length ) );
   constant M_SET_SRC_BIT_C : natural  := 0;
   constant M_SET_EDG_BIT_C : natural  := 1;
   constant M_SET_LVL_BIT_C : natural  := 2;
   constant M_SET_NPT_BIT_C : natural  := 3;
   constant M_SET_AUT_BIT_C : natural  := 4;
   constant M_SET_DCM_BIT_C : natural  := 5;
   constant M_SET_SCL_BIT_C : natural  := 6;
   constant M_SET_NSM_BIT_C : natural  := 7;
   constant M_SET_TGO_BIT_C : natural  := 8;

   constant CMD_LEN_C       : natural := acqCtlParmSizeBytes;

   type StateType is (ECHO, MASK, PROC, VALID, TRIG, DROP);

   function ACQ_CTL_PARM_INIT_F
      return AcqCtlParmType
   is
      variable v : AcqCtlParmType;
   begin
      v          := ACQ_CTL_PARM_INIT_C;
      v.nsamples := to_unsigned( MEM_DEPTH_G - 1, v.nsamples'length );
      return v;
   end function ACQ_CTL_PARM_INIT_F;

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
      p             => ACQ_CTL_PARM_INIT_F,
      count         => 0,
      trg           => '0'
   );

   signal r               : RegType := REG_INIT_C;
   signal rin             : RegType;

begin

   P_COMB : process ( r, r.p, mIb, rOb, ackIb ) is
      constant FOO_C   : std_logic_vector := toSlv( ACQ_CTL_PARM_INIT_F ); -- just to get the length/range
      variable v       : RegType;
      variable rb      : std_logic_vector(FOO_C'range);
   begin
      v       := r;

      rb      := toSlv( r.p );

      mOb     <= mIb;
      rIb     <= rOb;

      case ( r.state ) is
         when ECHO =>
            if ( (rOb and mIb.vld) = '1' ) then
               if ( parmsRdyIb = '0' ) then
                  mOb.lst <= '1';
                  if ( mIb.lst = '0' ) then
                     v.state := DROP;
                  end if;
               else
                  v.subCmd := subCommandAcqParmGet( mIb.dat );
                  v.state  := MASK;
                  v.count  := 0;
               end if;
            end if;

         when DROP  =>
            mOb.vld <= '0';
            rIb     <= '1';
            if ( (mIb.lst and mIb.vld) = '1' ) then
               v.state := ECHO;
            end if;

         when MASK  =>
            if ( (rOb and mIb.vld) = '1' ) then
               v.mask(7 + 8*r.count downto 8*r.count) := mIb.dat;
               mOb.dat                                <= r.mask(7 + 8*r.count downto 8*r.count);
               v.count                                := r.count + 1;
               if ( r.count = r.mask'length/8 - 1 ) then
                  v.state := PROC;
                  v.count := 0;
               end if;
               if ( mIb.lst = '1' ) then
                  v.state := ECHO;
               end if;
            end if;

         when PROC  =>
            if ( ( rOb and mIb.vld ) = '1' ) then
               mOb.dat <= rb(7 + 8*r.count downto 8*r.count);

               rb(7 + 8*r.count downto 8*r.count) := mIb.dat;

               v.p := toAcqCtlParmType( rb );

               if ( r.mask( M_SET_SRC_BIT_C ) = '0' ) then
                  v.p.src        := r.p.src;
               end if;
               if ( r.mask( M_SET_TGO_BIT_C ) = '0' ) then
                  v.p.trgDirOut  := r.p.trgDirOut;
               end if;
               if ( r.mask( M_SET_EDG_BIT_C ) = '0' ) then
                  v.p.rising     := r.p.rising;
               end if;
               if ( r.mask( M_SET_LVL_BIT_C ) = '0' ) then
                  v.p.lvl        := r.p.lvl;
                  v.p.hyst       := r.p.hyst;
               end if;
               if ( r.mask( M_SET_NPT_BIT_C ) = '0' ) then
                  v.p.nprets     := r.p.nprets;
               end if;
               if ( r.mask( M_SET_NSM_BIT_C ) = '0' ) then
                  v.p.nsamples   := r.p.nsamples;
               end if;
               if ( r.mask( M_SET_AUT_BIT_C ) = '0' ) then
                  v.p.autoTimeMs := r.p.autoTimeMs;
               end if;
               if ( r.mask( M_SET_DCM_BIT_C ) = '0' ) then
                  v.p.decm0      := r.p.decm0;
                  v.p.decm1      := r.p.decm1;
               end if;
               if ( r.mask( M_SET_SCL_BIT_C ) = '0' ) then
                  v.p.shift0     := r.p.shift0;
                  v.p.shift1     := r.p.shift1;
                  v.p.scale      := r.p.scale;
               end if;
               if ( ( r.count = CMD_LEN_C - 1 ) or ( mIb.lst = '1' ) ) then
                  if ( r.mask /= M_GET_C ) then
                     v.state  := VALID;
                     -- validate settings
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

         when VALID =>
            -- validate settings
            -- hold off sending last byte until trigger is acked
            mOb.vld  <= '0';
            if ( r.p.decm0 = 0 ) then
               v.p.decm1      := (others => '0');
            end if;
            if ( r.p.nsamples >= MEM_DEPTH_G ) then
               v.p.nsamples := to_unsigned( MEM_DEPTH_G - 1, v.p.nsamples'length );
            end if;
            if ( r.p.nprets > v.p.nsamples ) then
               v.p.nprets   := v.p.nsamples;
            end if;
            if ( r.p.src = EXT ) then
               v.p.trgDirOut := false;
            end if;

            v.trg    := not r.trg;
            v.state  := TRIG;

         when TRIG =>
            mOb.vld <= '0';
            if ( ackIb = r.trg ) then
               mOb.vld <= '1';
               mOb.lst <= '1';
               mOb.dat <= rb(7 + 8*r.count downto 8*r.count);
               v.state := ECHO;
            end if;
      end case;

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

   P_DBG : process ( clk ) is
   begin
      if ( rising_edge( clk ) ) then
         if ( r.state = TRIG and ackIb = r.trg ) then
            report "Source: " & integer'image( TriggerSrcType'pos( r.p.src ) );
            report "Rising: " & boolean'image( r.p.rising );
            report "Level : " & integer'image( to_integer( r.p.lvl ) );
            report "NPTS  : " & integer'image( to_integer( r.p.nprets ) );
            report "AutoTO: " & integer'image( to_integer( r.p.autoTimeMs ) );
            report "CIC0 D: " & integer'image( to_integer( r.p.decm0 ) );
            report "CIC1 D: " & integer'image( to_integer( r.p.decm1 ) );
            report "CIC0 S: " & integer'image( to_integer( r.p.shift0 ) );
            report "CIC1 S: " & integer'image( to_integer( r.p.shift1 ) );
            report "Scale : " & integer'image( to_integer( r.p.scale ) );
         end if;
      end if;
   end process P_DBG;


   parmsOb <= r.p;
   trgOb   <= r.trg;

end architecture rtl;
