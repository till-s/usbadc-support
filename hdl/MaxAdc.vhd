library ieee;
use     ieee.std_logic_1164.all;
use     ieee.numeric_std.all;
use     ieee.math_real.all;

use     work.BasicPkg.all;
use     work.CommandMuxPkg.all;
use     work.ILAWrapperPkg.all;
use     work.AcqCtlPkg.all;
use     work.SDRAMPkg.all;

entity MaxADC is
   generic (
      ADC_CLOCK_FREQ_G     : real                  := 130.0E6;
      MEM_DEPTH_G          : natural               := 1024;
      ADC_BITS_G           : natural               := 8;
      ONE_MEM_G            : boolean               := false;
      DISABLE_DECIMATORS_G : boolean               := false;
      RAM_BITS_G           : natural range 8 to 16 := 10;
      SDRAM_ADDR_WIDTH_G   : natural               := 0;
      USE_SDRAM_BUF_G      : boolean               := false;
      -- set to opposite of initial value of 'parmsTgl'
      -- this will start an initial acquistion
      INIT_ACQ_POL_G       : std_logic             := '1'
   );
   port (
      adcClk      : in  std_logic;
      adcRst      : in  std_logic;

      -- bit 0 is the DOR (overrange) bit
      adcDataA    : in  std_logic_vector(ADC_BITS_G downto 0);
      adcDataB    : in  std_logic_vector(ADC_BITS_G downto 0);

      -- SDRAM interface (if SDRAM sample buffer is used)
      sdramClk    : in  std_logic := '0';
      sdramReq    : out SDRAMReqType := SDRAM_REQ_INIT_C;
      sdramRep    : in  SDRAMRepType := SDRAM_REP_INIT_C;

      busClk      : in  std_logic;
      busRst      : in  std_logic;

      -- validity of parameters must be checked by provider
      parms       : in  AcqCtlParmType;
      -- toggling 'parmsTgl' initiates a new acquisition (aborting a pending one)
      parmsTgl    : in  std_logic;
      parmsAck    : out std_logic;

      busIb       : in  SimpleBusMstType;
      rdyIb       : out std_logic;

      busOb       : out SimpleBusMstType;
      rdyOb       : in  std_logic;

      status      : out std_logic_vector(7 downto 0);
      err         : out std_logic_vector(1 downto 0);

      extTrg      : in  std_logic := '0'
   );
end entity MaxADC;

architecture rtl of MaxADC is
   attribute KEEP           : string;
   attribute DONT_TOUCH     : string;
   attribute SYN_KEEP       : boolean; -- efinity for 'keep'; seems stronger than syn_preserve
   attribute MARK_DEBUG     : string;

   constant NUM_ADDR_BITS_C : natural := numBits(MEM_DEPTH_G);

   constant MS_TICK_PERIOD_C: natural := natural( round( ADC_CLOCK_FREQ_G / 1000.0 ) );

   -- I learned the following: when inferring RAM, XST always organizes into a parallel
   -- array of (depth x w), maximising 'depth'. This is 'normally' fine since
   -- it avoids using multiplexers (until depth 16k). I.e., using all 16 BRAMs of
   -- a 200 device results in 16 parallel 16k x 1 memories!
   -- However, this means that parity bits cannot be used.
   -- If RAM_BITS_G = 9 is to be used then some manual reconfiguration of the blocks
   -- including multiplexers must be implemented either completely manually (instantiating
   -- device macros) or by 'guiding' the inferal. In this case using ONE_MEM_G = false
   -- is preferable since the basic block is then 2kx9 instead of 1kx18, saving a little
   -- bit on the muxes.

   subtype  RamAddr         is unsigned( NUM_ADDR_BITS_C - 1 downto 0);

   subtype  RamWord         is std_logic_vector(RAM_BITS_G - 1 downto 0);

   subtype  ADCWord         is std_logic_vector(ADC_BITS_G - 1 downto 0);

   function memInfo return unsigned is
      constant w : natural := 16;
      constant b : natural := 512;
      variable i : natural := MEM_DEPTH_G;
      variable j : natural := MEM_DEPTH_G;
      variable r : unsigned(w - 1 downto 0);
   begin
      if ( i < b ) then
         j := 0;
      else
         j := i / b - 1;
      end if;
      assert ( ( i  mod  b ) /= 0 or ( j >= 2**w ) ) report "unable to accurately report memory size" severity warning;
      if ( j >= 2**w ) then
         j := 2**w - 1;
      end if;
      r := to_unsigned( j, r'length );
      return r;
   end memInfo;

   -- LHS adjusted word
   function toWord(constant x : RamWord) return std_logic_vector is
      variable v : std_logic_vector(15 downto 0);
   begin
      v                                      := (others => '0');
      v(v'left downto v'left - x'length + 1) := x;
      return v;
   end function toWord;

   function hiByte(constant x : RamWord) return std_logic_vector is
      variable v : std_logic_vector(15 downto 0);
   begin
      v := toWord( x );
      return v(15 downto 8);
   end function hiByte;

   function loByte(constant x : RamWord) return std_logic_vector is
      variable v : std_logic_vector(15 downto 0);
   begin
      v := toWord( x );
      return v( 7 downto 0);
   end function loByte;

   constant MSIZE_INFO_C    : unsigned(15 downto 0) := memInfo;

   constant FLAG_IDX_2B_C   : natural               := 0;

   function MSIZE_FLAGS_F return std_logic_vector is
      variable v : std_logic_vector(7 downto 0);
   begin
      v := (others => '0');
      v(FLAG_IDX_2B_C) := ite( RAM_BITS_G > 8 );
      return v;
   end function MSIZE_FLAGS_F;

   constant LD_BCNT_C       : natural               := ite( RAM_BITS_G > 8, 2, 1 );

   type     WrStateType     is (INIT, FILL, RUN, STOP1, STOP2, HOLD);

   type     WrRegType       is record
      state   : WrStateType;
      lstTrg  : std_logic;
      wasTrg  : boolean;
      nsmpls  : RamAddr;
      ovrA    : RamAddr;
      ovrB    : RamAddr;
      parms   : AcqCtlParmType;
      decmIs1 : boolean;
      tgl     : std_logic;
      armLvl  : signed(RamWord'range);
      tmrStrt : std_logic;
      fifoFul : std_logic;
   end record WrRegType;

   constant WR_REG_INIT_C   : WrRegType := (
      state   => INIT,
      lstTrg  => '0',
      wasTrg  => false,
      ovrA    => (others => '0'),
      ovrB    => (others => '0'),
      nsmpls  => (others => '0'),
      parms   => ACQ_CTL_PARM_INIT_C,
      decmIs1 => true,
      tgl     => '0',
      armLvl  => (others => '0'),
      tmrStrt => '0',
      fifoFul => '0'
   );

   function WR_REG_START_F(constant r : in WrRegType)
   return WrRegType is
      variable v : WrRegType;
   begin
      v       := WR_REG_INIT_C;
      v.tgl   := r.tgl;
      v.state := FILL;
      return v;
   end function WR_REG_START_F;

   type     RdStateType     is (ECHO, MSIZE, HDR, READ);

   type     RdRegType       is record
      state   : RdStateType;
      byteCnt : unsigned(LD_BCNT_C - 1 downto 0);
      busOb   : SimpleBusMstType;
      tgl     : std_logic;
      flush   : std_logic;
      fifoEmp : std_logic;
   end record RdRegType;

   function SIMPLE_BUS_MST_INIT_F return SimpleBusMstType is
      variable v : SimpleBusMstType := SIMPLE_BUS_MST_INIT_C;
   begin
      v.vld := '1'; -- always valid when muxed to rRd.busOb
      v.lst := '0';
      v.dat := (others => '0');
      return v;
   end function;

   constant RD_REG_INIT_C : RdRegType := (
      state   => ECHO,
      byteCnt => (others => '0'),
      busOb   => SIMPLE_BUS_MST_INIT_F,
      tgl     => '0',
      flush   => '0',
      fifoEmp => '0'
   );

   signal rRd       : RdRegType    := RD_REG_INIT_C;
   signal rinRd     : RdRegType;

   signal rWr       : WrRegType    := WR_REG_INIT_C;
   signal rinWr     : WrRegType;

   type   WrCCRegType is record
      ovrA    : std_logic;
      ovrB    : std_logic;
   end record WrCCRegType;

   signal rWrCC     : WrCCRegType;

   -- helps writing constraints
   attribute KEEP         of rWrCC   : signal is "TRUE";
   attribute SYN_KEEP     of rWrCC   : signal is true;

   signal memClk    : std_logic;
   signal filClk    : std_logic;

   -- external trigger synced into clock domain
   signal extTrgSyn : std_logic;

   -- raw ADC Data
   signal fdatA     : ADCWord;
   signal fdatB     : ADCWord;
   signal fdorA     : std_logic;
   signal fdorB     : std_logic;

   -- data into RAM
   signal wdatA     : RamWord;
   signal wdatB     : RamWord;
   signal wdorA     : std_logic;
   signal wdorB     : std_logic;
   signal trg       : std_logic := '0';
   signal armed     : boolean   := false;
   -- keep data in sync with registered trigger
   signal wdatAin   : RamWord;
   signal wdatBin   : RamWord;
   signal wdorAin   : std_logic;
   signal wdorBin   : std_logic;
   signal trgin     : std_logic;
   signal armin     : boolean;

   signal wrDon             : std_logic;
   signal wrEna             : std_logic;
   signal wrFul             : std_logic;
   signal wrDat             : std_logic_vector(2*RamWord'length downto 0);
   signal rdDat             : std_logic_vector(2*RamWord'length downto 0);
   signal rdEmp             : std_logic;
   signal rdEna             : std_logic;

   signal memFull           : std_logic;
   signal rdTgl             : std_logic;
   signal wrTgl             : std_logic;
   signal wrDecm            : std_logic := '1';
   signal extTrgSynDelayed  : std_logic := '0'; -- must be delayed by decimation filter group delay

   signal acqTglIb          : std_logic;
   signal acqTglOb          : std_logic := INIT_ACQ_POL_G;

   signal msTickCounter     : natural range 0 to MS_TICK_PERIOD_C - 1 := MS_TICK_PERIOD_C - 1;
   signal msTimer           : unsigned( 15 downto 0 )                 := AUTO_TIME_STOP_C;
   signal msTimerExpired    : boolean   := false;
   signal msTimerStartDly   : std_logic;

   signal lparms            : AcqCtlParmType := ACQ_CTL_PARM_INIT_C;

   signal statusLoc         : std_logic_vector(status'range) := (others => '0');

begin

   assert MEM_DEPTH_G mod 1024 = 0 and MEM_DEPTH_G >= 1024 report "Cannot report accurate memory size" severity warning;

   lparms  <= rWr.parms;

   memClk  <= adcClk;
   filClk  <= memClk;

   memFull <= '1' when (rWr.state = HOLD) else '0';
   wrEna   <= not memFull and wrDecm;

   U_RD_SYNC : entity work.SynchronizerBit
      port map (
         clk       => memClk,
         rst       => '0',
         datInp(0) => rRd.tgl,
         datOut(0) => rdTgl
      );

   U_WR_SYNC : entity work.SynchronizerBit
      port map (
         clk       => busClk,
         rst       => '0',
         datInp(0) => rWr.tgl,
         datOut(0) => wrTgl
      );

   U_EXT_TRG_SYNC : entity work.SynchronizerBit
      port map (
         clk       => filClk,
         rst       => '0',
         datInp(0) => extTrg,
         datOut(0) => extTrgSyn
      );

   fdatA         <= adcDataA(ADC_BITS_G downto 1);
   fdorA         <= adcDataA(                  0);
   fdatB         <= adcDataB(ADC_BITS_G downto 1);
   fdorB         <= adcDataB(                  0);

   rWrCC.ovrA    <= toSl(rWr.ovrA /= 0);
   rWrCC.ovrB    <= toSl(rWr.ovrB /= 0);

   -- ise doesn't seem to properly handle nested records
   -- (getting warning about rRd.busOb missing from sensitivity list)
   P_RD_COMB : process (rRd, rRd.busOb, busIb, rdyOb, rdEmp, rdDat, wrTgl, rWrCC) is
      variable v       : RdRegType;
      variable rdatA   : RamWord;
      variable rdatB   : RamWord;
      variable rdLst   : std_logic;

  begin
      v          := rRd;

      rdatA      := rdDat(   RAM_BITS_G - 1 downto          0 );
      rdatB      := rdDat( 2*RAM_BITS_G - 1 downto RAM_BITS_G );
      rdLst      := rdDat( rdDat'left                         );

      rdyIb      <= '1'; -- drop anything extra;

      busOb      <= rRd.busOb;
      rdEna      <= '0';

      v.flush    := '0';

      -- compute byteCnt; covers all relevant states
      if ( rdyOb = '1' ) then
         v.byteCnt   := rRd.byteCnt - 1;
         v.busOb.vld := '0';
      end if;

      case ( rRd.state ) is
         when ECHO =>
            v.byteCnt   := (others => '0');

            busOb       <= busIb;
            busOb.lst   <= '0';
            rdyIb       <= rdyOb;
            v.busOb.vld := '1';

            if ( (rdyOb and busIb.vld) = '1' ) then
               if ( CMD_ACQ_MSIZE_C = subCommandAcqGet( busIb.dat ) ) then
                  v.state     := MSIZE;
                  v.busOb.dat := std_logic_vector( MSIZE_INFO_C(7 downto 0) );
                  v.busOb.lst := '0';
               elsif ( ( rdEmp = '0' ) and ( CMD_ACQ_READ_C = subCommandAcqGet( busIb.dat ) ) ) then
                  v.state        := HDR;
                  v.busOb.dat    := (others => '0');
                  v.busOb.dat(0) := rWrCC.ovrA;
                  v.busOb.dat(1) := rWrCC.ovrB;
                  v.busOb.lst    := '0';
               else
                  busOb.lst <= '1';
                  if ( rdEmp = '0' ) then  -- implies CMD_ACQ_FLUSH_C = subCommandAcqGet( busIb.dat )
                     v.flush := '1';
                     v.tgl   := wrTgl;
                  end if;
               end if;
            end if;

         when MSIZE =>
            if ( rdyOb = '1' ) then -- busOb.vld is '1' at this point
               v.busOb.vld := '1';
               if ( rRd.byteCnt = 0 ) then
                  v.busOb.dat := std_logic_vector( MSIZE_INFO_C(15 downto 8) );
               elsif ( rRd.busOb.lst = '1' ) then
                  v.state     := ECHO;
               else
                  v.busOb.dat := MSIZE_FLAGS_F;
                  v.busOb.lst := '1';
               end if;
            end if;

         when HDR  =>
            if ( rdyOb = '1' ) then -- busOb.vld is '1' at this point
               -- rRd.byteCnt is 0 here
               v.busOb.dat := (others => '0');
               v.state     := READ;
               v.busOb.vld := '1';
            end if;

         when READ =>
            if ( rdEmp = '1' ) then
               -- if there is nothing to read when byteCnt must not advance
               v.byteCnt := rRd.byteCnt;
            end if;
            if ( rdyOb = '1' ) then -- busOb.vld  is '1' at this point
               v.busOb.vld    := not rdEmp;
               if ( rRd.byteCnt = 0 ) then
                  -- consume
                  -- note: we can never get here when rdEmp is '1'
                  --         a) first time we enter rdEmp is '0' since ECHO state
                  --         b) while 'rdEmp' busOb.vld is deasserted and byteCnt 
                  --            does not make progress
                  rdEna       <= '1';
                  v.busOb.dat := hiByte( rdatB );
                  -- is the end reached 
                  v.busOb.lst := rdLst;
               else
                  if ( rRd.busOb.lst = '1' ) then
                     v.state := ECHO;
                     v.tgl   := wrTgl;
                  end if;
                  if ( ( rRd.byteCnt'length /= 2 ) or ( rRd.byteCnt(0) = '0' ) ) then
                     v.busOb.dat := hiByte( rdatA );
                  elsif ( rRd.byteCnt(rRd.byteCnt'left) = '1' ) then
                     v.busOb.dat := loByte( rdatA );
                  else -- byteCnt = "01"
                     v.busOb.dat := loByte( rdatB );
                  end if;
               end if;
            end if;
      end case;

      rinRd <= v;
   end process P_RD_COMB;

   P_RD_SEQ : process ( busClk ) is
   begin
      if ( rising_edge( busClk ) ) then
         if ( busRst = '1' ) then
            rRd <= RD_REG_INIT_C;
         else
            rRd <= rinRd;
         end if;
      end if;
   end process P_RD_SEQ;

   GEN_MEM_ILA : if ( false ) generate
   begin
      U_ILA_MEM : component ILAWrapper
         port map (
            clk  => memClk,
            trg0 => adcDataA(ADC_BITS_G downto ADC_BITS_G - 8 + 1),
            trg1 => adcDataB(ADC_BITS_G downto ADC_BITS_G - 8 + 1),
            trg2 => x"00",
            trg3 => x"00"
         );
   end generate GEN_MEM_ILA;

   -- compare the un-delayed wdatAin/wdatBin to produce the registered
   -- trigger 'trg'...
   P_TRG : process ( rWr, lparms, wdatAin, wdatBin, extTrgSynDelayed, armed ) is
      variable v      : std_logic;
      variable l      : signed(wdatAin'range);
      variable a      : signed(wdatAin'range);
      variable x      : signed(wdatAin'range);
   begin
      armin <= armed;
      l := lparms.lvl(lparms.lvl'left downto lparms.lvl'left - wdatAin'length + 1);

      if ( lparms.src = CHB ) then
         x := signed(wdatBin);
      else
         x := signed(wdatAin);
      end if;

      if ( ( x < rWr.armLvl ) = lparms.rising ) then
         armin <= true;
      end if;

      v := '0';
      case ( lparms.src ) is
         when CHA | CHB =>
            if ( ( ( x >= l ) = lparms.rising ) and armed ) then
               v := '1';
               -- if this happens in the FILL stage then
               -- the trigger is ignored and we must reset 'armed'
               if ( rWr.state = FILL ) then
                  v     := '0';
                  armin <= false;
               end if;
            end if;
         when EXT =>
            v := extTrgSynDelayed xnor toSl( lparms.rising );
         when others => -- manual
            -- handle separately
      end case;

      trgin <= v;
   end process P_TRG;

   GEN_TRG_ILA : if ( false ) generate
   begin
      U_ILA_TRG : component ILAWrapper
         port map (
            clk  => filClk,
            trg0 => wdatAin(wdatAin'left downto wdatAin'left - 7),
            trg1 => std_logic_vector( lparms.lvl(lparms.lvl'left downto lparms.lvl'left - 7) ),
            trg2 => std_logic_vector( rWr.armLvl( rWr.armLvl'left downto rWr.armLvl'left - 7) ),
            trg3(0) => trgin,
            trg3(1) => toSl( armin ),
            trg3(2) => acqTglIb,
            trg3(3) => toSl( armed ),
            trg3(4) => trg,
            trg3(5) => toSl( lparms.rising ),
            trg3(6) => acqTglOb,
            trg3(7) => '0'
         );
   end generate GEN_TRG_ILA;


   P_TICK : process ( memClk ) is
   begin
      if ( rising_edge( memClk ) ) then
         -- not absolutely precise timing...
         msTimerStartDly <= rWr.tmrStrt;
         if ( msTimer = 0 ) then
            msTimerExpired <= true;
         end if;
         if ( msTickCounter = 0 ) then
            msTickCounter <= MS_TICK_PERIOD_C - 1;
            if ( msTimer /= AUTO_TIME_STOP_C ) then
               msTimer <= msTimer - 1;
            end if;
         else
            msTickCounter <= msTickCounter - 1;
         end if;
         if ( (rWr.tmrStrt and not msTimerStartDly) = '1' ) then
           msTimer        <= lparms.autoTimeMs;
           msTimerExpired <= false;
         end if;
      end if;
   end process P_TICK;

   P_WR_COMB : process ( rWr, lparms, trg, rdTgl, wrFul, wdatA, wdorA, wdatB, wdorB, msTimerExpired ) is
      variable v : WrRegType;
      variable a : signed( lparms.lvl'length downto 0 );
      variable s : std_logic;
   begin

      v         := rWr;
      v.tmrStrt := '0';

      wrDat     <= '0' & wdatB & wdatA;

      if ( lparms.rising ) then
         a := resize( lparms.lvl, a'length ) - signed( resize( lparms.hyst, a'length ) );
      else
         a := resize( lparms.lvl, a'length ) + signed( resize( lparms.hyst, a'length ) );
      end if;
      s := a(a'left);
      if ( s /= a(a'left - 1) ) then
         -- overflow
         a := ( a'left => s, a'left - 1 => s, others => (not s) );
      end if;

      -- throw away the carry/overflow bit
      v.armLvl := a( a'left - 1 downto a'left - 1 - v.armLvl'length + 1 );

      if ( rWr.state = FILL or rWr.state = RUN ) then
         v.lstTrg  := trg;
         v.nsmpls  := rWr.nsmpls + 1;
         -- remember overrange 'seen' during the last MEM_DEPTH_G samples
         if ( wdorA = '1' ) then
            -- ASSUME: validity of nsamples has been checked against MEM_DEPTH_G
            v.ovrA := resize( lparms.nsamples, v.ovrA'length );
         elsif ( rWr.ovrA /= 0 ) then
            v.ovrA := rWr.ovrA - 1;
         end if;
         if ( wdorB = '1' ) then
            -- ASSUME: validity of nsamples has been checked against MEM_DEPTH_G
            v.ovrB := resize( lparms.nsamples, v.ovrB'length );
         elsif ( rWr.ovrB /= 0 ) then
            v.ovrB := rWr.ovrB - 1;
         end if;
         if ( wrFul = '1' ) then
            v.fifoFul := '1';
         end if;
      end if;

      case ( rWr.state ) is
         when INIT       =>

         when FILL       =>
            if ( rWr.nsmpls >= lparms.nprets ) then
               v.state   := RUN;
               -- discard oldest sample (need 1 to fill 'lstTrg')
               v.nsmpls  := resize( lparms.nprets, v.nsmpls'length );
               v.tmrStrt := '1';
            end if;

         when RUN        =>
            if ( not rWr.wasTrg and ( ( ( not rWr.lstTrg and trg ) = '1' ) or msTimerExpired ) ) then
               v.wasTrg := true;
            end if;
            if ( v.wasTrg ) then
               -- lparms.nsamples is the actual number of samples - 1
               -- comparison is ok, this last sample still
               -- is stored and in 'STOP1/2/HOLD' state rWr.nsmpls = nsamples + 1
               if ( lparms.nsamples = rWr.nsmpls ) then
                  v.state := STOP1;
               end if;
            else
               -- discard oldest sample
               v.nsmpls := rWr.nsmpls;
            end if;

         when STOP1      =>
            wrDat             <= (others => '0');
            wrDat(wrDat'left) <= '1';
            if ( rWr.nsmpls'length > 16 ) then
               wrDat( 15 downto 0 )      <= std_logic_vector( rWr.nsmpls( 15 downto 0 ) );
            else
               wrDat( rWr.nsmpls'range ) <= std_logic_vector( rWr.nsmpls );
            end if;
            v.state  := STOP2;

         when STOP2      =>
            wrDat             <= (others => '0');
            wrDat(wrDat'left) <= '1';
            if ( rWr.nsmpls'length > 16 ) then
               wrDat( rWr.nsmpls'left - 16 downto 0 ) <= std_logic_vector( rWr.nsmpls( rWr.nsmpls'left downto 16 ) );
            end if;
            v.state  := HOLD;
            v.tgl    := not rWr.tgl;

         when HOLD       =>
            if ( rdTgl = rWr.tgl ) then
               v.nsmpls := to_unsigned( 0, v.nsmpls'length );
               v.state  := FILL;
               v.wasTrg := false;
               v.ovrA   := to_unsigned( 0, v.ovrA'length );
               v.ovrB   := to_unsigned( 0, v.ovrB'length );
            end if;
      end case;
      rinWr <= v;
   end process P_WR_COMB;

   U_WR_SYNC_ACQ : entity work.SynchronizerBit
      generic map (
         RSTPOL_G  => INIT_ACQ_POL_G
      )
      port map (
         clk       => memClk,
         rst       => '0',
         datInp(0) => parmsTgl,
         datOut(0) => acqTglIb
      );

   U_RD_SYNC_ACQ : entity work.SynchronizerBit
      generic map (
         RSTPOL_G  => INIT_ACQ_POL_G
      )
      port map (
         clk       => busClk,
         rst       => '0',
         datInp(0) => acqTglOb,
         datOut(0) => parmsAck
      );

   P_WR_SEQ : process ( memClk ) is
   begin
      if ( rising_edge( memClk ) ) then
         if ( ( acqTglOb /= acqTglIb ) and ( rWr.state /= STOP1 ) and ( rWr.state /= STOP2 ) ) then
            -- sit out STOP states; let proceed into HOLD (
            -- assume validity of parameters has been checked
            -- by the provider!
            acqTglOb    <= acqTglIb;

            if ( rWr.state /= HOLD ) then
               -- if we are already in HOLD state then we can't reset
               -- the state machine - the reader owns the buffer and
               -- we may only restart writing once the reader releases.
               -- Since we are not writing ATM it is OK to just update
               -- the parameters, though.
               rWr      <= WR_REG_START_F( rWr );
            end if;

            rWr.parms   <= parms;
            rWr.decmIs1 <= (parms.decm0 = 0);
         elsif ( ( wrDecm = '1' ) and ( rWr.state /= INIT ) ) then
            rWr         <= rinWr;
           -- register trigger and delay data to remain in-sync
            armed       <= armin;
            trg         <= trgin;
            wdatA       <= wdatAin;
            wdatB       <= wdatBin;
            wdorA       <= wdorAin;
            wdorB       <= wdorBin;
         end if;
         if ( ( rWr.state = HOLD ) or ( acqTglOb /= acqTglIb ) ) then
            armed       <= false;
            trg         <= '0';
         end if;
      end if;
   end process P_WR_SEQ;

   GEN_BUS_ILA : if ( false ) generate
      signal bTrg3 : std_logic_vector(7 downto 0);
   begin
      bTrg3(0)          <= rRd.byteCnt(rRd.byteCnt'left);
      bTrg3(1)          <= rdyOb;
      bTrg3(2)          <= '0';
      bTrg3(4 downto 3) <= std_logic_vector( to_unsigned( RdStateType'pos( rRd.state ), 2 ) );
      bTrg3(7 downto 5) <= rRd.busOb.dat(7 downto 5);

      U_ILA_REG : component ILAWrapper
         port map (
            clk  => busClk,
            trg0 => hiByte( rdDat(   RAM_BITS_G - 1 downto          0 ) ),
            trg1 => hiByte( rdDat( 2*RAM_BITS_G - 1 downto RAM_BITS_G ) ),
            trg2 => open,
            trg3 => bTrg3
         );
   end generate GEN_BUS_ILA;

   G_DECIMATORS : if ( not DISABLE_DECIMATORS_G ) generate
      -- HW multiplier (factor) width
      constant MUL_FACT_W_C         : natural := 18;

      -- at least one extra bit; because we cannot scale
      -- precisely with just a right-shift.
      constant STG0_OBITS_C         : natural := RAM_BITS_G + 1;

      constant STG0_STGS_C          : natural := 4;
      constant STG0_LD_MAX_DCM_C    : natural := 4;

      constant STG0_SCL_LD_ONE_C    : natural := 16;
      subtype  Stg0ScaleType        is signed(STG0_SCL_LD_ONE_C + 1 downto 0);
      type     Stg0ScaleArray       is array (natural range <>) of Stg0ScaleType;

      constant STG0_W_C             : natural := fdatA'length + STG0_LD_MAX_DCM_C*STG0_STGS_C;

      constant STG1_OBITS_C         : natural := MUL_FACT_W_C;
      constant STG1_STGS_C          : natural := 4;
      constant STG1_LD_MAX_DCM_C    : natural :=12;


      constant STG1_W_C             : natural := STG0_OBITS_C + STG1_LD_MAX_DCM_C*STG1_STGS_C;

      -- use some multiplier bits for the shifting - we
      -- use a stride in the shifter and can then
      -- do the last bits in the multiplier.
      -- In the special case of having powers of two for the
      -- number of CIC stages and for the stride then it becomes
      -- easy to compute the breakpoints in the decimation
      -- rate where we have to switch the shifter:
      --   shifter:
      --      N = 2^(STRIDE * shift)
      --   CIC amplification:  decm^STAGES
      -- Thus,  shift = log2(decimation) * STAGES/STRIDE
      --
      -- The hardware multiplier has 18 bits, thus we can
      -- handle a growth of 8 bits in the multplier which
      -- and thus a decimation ratio of 2^STRIDE/STAGES = 4
      -- whenever his ratio is a is a power of two computing
      -- the breakpoints is simple.
      constant STG1_STRIDE_C        : natural := 8; -- chose so that stride/stages is a natural
                                                    -- number (limited by multiplier width to
                                                    -- something between 1 and 9 or 10.

      constant STG1_RAT_C           : natural := STG1_STRIDE_C / STG1_STGS_C;

      constant STG1_SHF_W_C         : natural := numBits( (STG1_W_C - 1) / STG1_STRIDE_C );

      signal   stg1Shf              : std_logic_vector(STG1_SHF_W_C - 1 downto 0);
      -- ensure this vector'length is a multiple of the STRIDE/STAGES ratio
      signal   stg1ShfSel           : std_logic_vector( ( (STG1_LD_MAX_DCM_C + STG1_RAT_C - 1)/STG1_RAT_C) * STG1_RAT_C  - 1 downto 0);

      signal   stg0Ctl              : std_logic_vector(STG0_STGS_C downto 0);
      signal   stg0ShfCtl           : boolean;

      signal   cenOut0              : std_logic;


      signal   cenCic1              : std_logic;
      attribute KEEP         of cenCic1 : signal is "TRUE";
      attribute SYN_KEEP     of cenCic1 : signal is true;

      -- one extra bit because of sign
      signal   stg0Scl              : Stg0ScaleType;

      signal   stg0CicDatA          : signed(STG0_W_C - 1 downto 0);
      signal   stg0CicDorA          : std_logic;
      signal   stg0CicTrgA          : std_logic;
      signal   stg0ShfDatA          : signed(stg0Scl'length + stg0CicDatA'length - 1 downto 0);
      signal   stg0ShfDorA          : std_logic;
      signal   stg0ShfTrgA          : std_logic;

      signal   stg0CicDatB          : signed(STG0_W_C - 1 downto 0);
      signal   stg0CicDorB          : std_logic;
      signal   stg0ShfDatB          : signed(stg0Scl'length + stg0CicDatB'length - 1 downto 0);
      signal   stg0ShfDorB          : std_logic;

      signal   stg0DatA             : signed( STG0_OBITS_C - 1 downto 0 );
      signal   stg0DatB             : signed( STG0_OBITS_C - 1 downto 0 );

      signal   stg1Ctl              : std_logic_vector(STG1_STGS_C downto 0);

      signal   cenOut1              : std_logic;

      signal   stg1CicDatA          : signed(STG1_W_C - 1 downto 0);
      signal   stg1CicDorA          : std_logic;
      signal   stg1CicTrgA          : std_logic;
      signal   stg1ShfDatA          : std_logic_vector(STG1_W_C - 1 downto 0);
      signal   stg1ShfDorA          : std_logic;
      signal   stg1ShfTrgA          : std_logic;

      signal   stg1CicDatB          : signed(STG1_W_C - 1 downto 0);
      signal   stg1CicDorB          : std_logic;
      signal   stg1ShfDatB          : std_logic_vector(STG1_W_C - 1 downto 0);
      signal   stg1ShfDorB          : std_logic;

      signal   stg1DatA             : std_logic_vector( STG1_OBITS_C - 1 downto 0 );
      signal   stg1DatB             : std_logic_vector( STG1_OBITS_C - 1 downto 0 );

      signal   mulaA                : signed(STG1_OBITS_C - 1 downto 0)   := (others => '0');
      signal   mulaB                : signed(STG1_OBITS_C - 1 downto 0)   := (others => '0');
      signal   mulbA                : signed(STG1_OBITS_C - 1 downto 0)   := (others => '0');
      signal   mulbB                : signed(STG1_OBITS_C - 1 downto 0)   := (others => '0');
      signal   mulpA                : signed(2*STG1_OBITS_C - 1 downto 0) := (others => '0');
      signal   mulpB                : signed(2*STG1_OBITS_C - 1 downto 0) := (others => '0');

      signal   mulDorDlyA           : std_logic_vector(1 downto 0)        := (others => '0');
      signal   mulTrgDlyA           : std_logic_vector(1 downto 0)        := (others => '0');
      signal   mulDorDlyB           : std_logic_vector(1 downto 0)        := (others => '0');

   -- parametrization of the stage-0 multiplier-shifter; break point is when
   -- input data uses the full width of the factor: max |data| is |-2**7|
   --  ->  decim**STG0_STGS * 2**7 <= 2**(FACT_WIDTH - 1)
   --  ->  decim <= 2**( (FACT_WIDTH - 1 - (fdat'length - 1) ) / STG0_STGS )
      constant DCM0_BRK_C           : natural := natural( floor( 2.0**(real(18 - fdatA'length)/real(STG0_STGS_C)) ) );

      constant LD_PRE_SCALE_C       : natural := stg0CicDatA'length - MUL_FACT_W_C;

      function CIC_SCL_F(
         constant decm : in natural;
         constant stgs : in natural;
         constant ld_1 : in natural
      ) return signed is
         variable vr : real;
         variable vi : natural;
         variable one: natural;
      begin
          if ( decm <= DCM0_BRK_C ) then
            one := 2**ld_1;
          else
            one := 2**(ld_1 + LD_PRE_SCALE_C);
          end if;
          vr := round( real(one) / real(decm)**stgs );
          vi := natural( vr );
          if ( vi * decm**stgs > one ) then
             vi := vi - 1;
          end if;
         return to_signed( vi, ld_1 + 2 );
      end function CIC_SCL_F;

      function STG0_SCL_FILL_F return Stg0ScaleArray is
         variable v : Stg0ScaleArray(2**STG0_LD_MAX_DCM_C - 1 downto 0);
      begin
         for i in v'range loop
            -- decimation register value is actual decimation - 1
            v(i) := CIC_SCL_F( i + 1, STG0_STGS_C, STG0_SCL_LD_ONE_C );
         end loop;
         return v;
      end function STG0_SCL_FILL_F;

      constant STG0_SHF_TBL_C : Stg0ScaleArray := STG0_SCL_FILL_F;

   begin

      U_CIC0_A : entity work.CicFilter
         generic map (
            DATA_WIDTH_G   => fdatA'length,
            LD_MAX_DCM_G   => STG0_LD_MAX_DCM_C,
            NUM_STAGES_G   => STG0_STGS_C,
            DCM_MASTER_G   => true
         )
         port map (
            clk            => filClk,
            rst            => adcRst,

            decmInp        => lparms.decm0,

            cenbOut        => stg0Ctl,
            cenbInp        => open,

            dataInp        => signed(fdatA),
            dovrInp        => fdorA,
            trigInp        => extTrgSyn,

            dataOut        => stg0CicDatA,
            dovrOut        => stg0CicDorA,
            trigOut        => stg0CicTrgA,

            strbOut        => cenOut0
         );

      U_CIC0_B : entity work.CicFilter
         generic map (
            DATA_WIDTH_G   => fdatB'length,
            LD_MAX_DCM_G   => STG0_LD_MAX_DCM_C,
            NUM_STAGES_G   => STG0_STGS_C,
            DCM_MASTER_G   => false
         )
         port map (
            clk            => filClk,
            rst            => adcRst,

            decmInp        => lparms.decm0,

            cenbOut        => open,
            cenbInp        => stg0Ctl,

            dataInp        => signed(fdatB),
            dovrInp        => fdorB,

            dataOut        => stg0CicDatB,
            dovrOut        => stg0CicDorB,

            strbOut        => open
         );

      -- use multipliers for scaling
      -- max. range for data: +/- 2**17
      --  -> if data  < 2**17 :    p = (data            * scale) / NORM
      --  -> if data >= 2**17 :    p = (data/PRE_SCALE) * scale) / (NORM * PRE_SCALE)
      -- numBits(data) - LD_PRE_SCALE <= 18 (multiplier width)
      --   LD_PRE_SCALE >= STG0_W_C - 18
      -- Condition for data < 2**17:  decm**STG0_STGS * data_max <= 2**17
      --  STG0_STGS * log2(decm) <= 17 - log2(|data_max|)
      --  -> decm <= floor( 2**( (17 - log2(|data_max|)) / STG0_STGS ) )
      --
      -- For larger decimation the scale becomes small (1/gain) introducing more error.
      -- This is also the regime where we have to use the pre-scaler; we can therefore
      -- normalize the scale to (NORM * PRE_SCALE) and use more bits of the scale.
      -- We simply omit the post-left shift in the MulShifter (NO_POSTSHF_G => true).

      -- STG0_W_C : ADC_BITS_G + ld(max_gain)
      -- LD_PRE_SCALE = STG0_W_C - FACT_WIDTH = ADC_BITS_G + ld(max_gain) - FACT_WIDTH
      -- in order to avoid (significant) truncation:
      --     once prescaling kicks in, i.e., for decim = breakpoint_decm + 1
      --     the shift should not erase bits; ld( (bkpt_decm + 1)**STG0_STGS_C ) > LD_PRE_SCALE
      -- ld_brkpt_gain + adc_bits_g = FACT_WIDTH
      -- FACT_WIDTH - ADC_BITS_G > ADC_BITS_G + ld_max_gain - FACT_WIDTH
      -- 2*FACT_WIDTH > 2*ADC_BITS_G + ld_max_gain
      -- If FACT_WIDTH = 18 , ld_max_gain = 16 => ADC_BITS_G < 10
      -- but this is a bit too conservative; the real equation is
      --   ld( (bkpt_decm + 1)**STG0_STGS_C ) > ADC_BITS_G - FACT_WIDTH_G + ld_max_gain
      -- which for FACT_WIDTH = 18, ADC_BITS = 10, STG0_STGS = 4, ld_max_gain = 16
      -- yields
      --   ld ( 5 ) * 4 > 10 - 18 + 16 = 8  =>  ld(5) > 2 which is OK.

      assert 2*MUL_FACT_W_C >= 2*ADC_BITS_G + STG0_STGS_C*STG0_LD_MAX_DCM_C
         report "Need a pre-shifter if stage 0 required width is > 18" severity failure;

      stg0ShfCtl <= (to_integer( lparms.decm0 ) <= DCM0_BRK_C - 1);
      stg0Scl    <= STG0_SHF_TBL_C( to_integer( lparms.decm0 ) );

      U_SHF0_A : entity work.MulShifter
         generic map (
            FBIG_WIDTH_G   => stg0CicDatA'length,
            FACT_WIDTH_G   => MUL_FACT_W_C,
            SCAL_WIDTH_G   => stg0Scl'length,
            AUXV_WIDTH_G   => 2,
            NO_POSTSHF_G   => true
         )
         port map (
            clk            => filClk,
            rst            => adcRst,
            cen            => cenOut0,

            fbigInp        => stg0CicDatA,
            scalInp        => stg0Scl,
            auxvInp(0)     => stg0CicDorA,
            auxvInp(1)     => stg0CicTrgA,

            ctl            => stg0ShfCtl,

            prodOut        => stg0ShfDatA,
            auxvOut(0)     => stg0ShfDorA,
            auxvOut(1)     => stg0ShfTrgA
         );

      U_SHF0_B : entity work.MulShifter
         generic map (
            FBIG_WIDTH_G   => stg0CicDatB'length,
            FACT_WIDTH_G   => MUL_FACT_W_C,
            SCAL_WIDTH_G   => stg0Scl'length,
            AUXV_WIDTH_G   => 1,
            NO_POSTSHF_G   => true
         )
         port map (
            clk            => filClk,
            rst            => adcRst,
            cen            => cenOut0,

            fbigInp        => stg0CicDatB,
            scalInp        => stg0Scl,
            auxvInp(0)     => stg0CicDorB,

            ctl            => stg0ShfCtl,

            prodOut        => stg0ShfDatB,
            auxvOut(0)     => stg0ShfDorB
         );

      stg0DatA <= resize( shift_right( stg0ShfDatA, STG0_SCL_LD_ONE_C + ADC_BITS_G - RAM_BITS_G ), stg0DatA'length );
      stg0DatB <= resize( shift_right( stg0ShfDatB, STG0_SCL_LD_ONE_C + ADC_BITS_G - RAM_BITS_G ), stg0DatB'length );

      U_CIC1_A : entity work.CicFilter
         generic map (
            DATA_WIDTH_G   => stg0DatA'length,
            LD_MAX_DCM_G   => STG1_LD_MAX_DCM_C,
            NUM_STAGES_G   => STG1_STGS_C,
            DCM_MASTER_G   => true
         )
         port map (
            clk            => filClk,
            rst            => adcRst,

            cen            => cenCic1,

            decmInp        => lparms.decm1(STG1_LD_MAX_DCM_C - 1 downto 0),

            cenbOut        => stg1Ctl,
            cenbInp        => open,

            dataInp        => stg0DatA,
            dovrInp        => stg0ShfDorA,
            trigInp        => stg0ShfTrgA,

            dataOut        => stg1CicDatA,
            dovrOut        => stg1CicDorA,
            trigOut        => stg1CicTrgA,

            strbOut        => cenOut1
         );

      assert ( STG1_STGS_C = 4 ) report "rework is required to change the number CIC1 stages" severity failure;

      P_SHF_SEL : process ( lparms.decm1, stg1ShfSel ) is
         constant ZER_C : std_logic_vector(STG1_RAT_C - 1 downto 0) := (others => '0');
      begin
         stg1ShfSel <= (others => '0');
         stg1ShfSel(STG1_LD_MAX_DCM_C - 1 downto 0) <= std_logic_vector( lparms.decm1(STG1_LD_MAX_DCM_C - 1 downto 0) );

         -- default
         stg1Shf    <= (others => '0');
         -- compute correct shift for decimation ratio
         -- we can infer from just looking at the bits in decim1 -- because
         -- STG1_RAT_C is integer...
         L_SHF_SEL : for i in stg1ShfSel'length/STG1_RAT_C - 1 downto 0 loop
            if ( stg1ShfSel( (i + 1) * STG1_RAT_C - 1 downto i * STG1_RAT_C ) /= ZER_C ) then
               stg1Shf <= std_logic_vector( to_unsigned( i , stg1Shf'length ) );
               exit L_SHF_SEL; -- break this loop
            end if;
         end loop;
      end process P_SHF_SEL;

      U_SHF1_A : entity work.PipelinedRShifter
         generic map (
            DATW_G         => STG1_W_C,
            AUXW_G         => 2,
            SIGN_EXTEND_G  => true,
            PIPL_SHIFT_G   => false,
            STRIDE_G       => STG1_STRIDE_C
         )
         port map (
            clk            => filClk,
            rst            => adcRst,
            cen            => cenOut1,

            shift          => stg1Shf,

            datInp         => std_logic_vector( stg1CicDatA ),
            auxInp(0)      => stg1CicDorA,
            auxInp(1)      => stg1CicTrgA,

            datOut         => stg1ShfDatA,
            auxOut(0)      => stg1ShfDorA,
            auxOut(1)      => stg1ShfTrgA
         );

      U_CIC1_B : entity work.CicFilter
         generic map (
            DATA_WIDTH_G   => stg0DatB'length,
            LD_MAX_DCM_G   => STG1_LD_MAX_DCM_C,
            NUM_STAGES_G   => STG1_STGS_C,
            DCM_MASTER_G   => false
         )
         port map (
            clk            => filClk,
            rst            => adcRst,

            cen            => cenCic1,

            decmInp        => lparms.decm1(STG1_LD_MAX_DCM_C - 1 downto 0),

            cenbOut        => open,
            cenbInp        => stg1Ctl,

            dataInp        => stg0DatB,
            dovrInp        => stg0ShfDorB,

            dataOut        => stg1CicDatB,
            dovrOut        => stg1CicDorB,

            strbOut        => open
         );

      U_SHF1_B : entity work.PipelinedRShifter
         generic map (
            DATW_G         => STG1_W_C,
            AUXW_G         => 1,
            SIGN_EXTEND_G  => true,
            PIPL_SHIFT_G   => false,
            STRIDE_G       => STG1_STRIDE_C
         )
         port map (
            clk            => filClk,
            rst            => adcRst,
            cen            => cenOut1,

            shift          => stg1Shf,

            datInp         => std_logic_vector( stg1CicDatB ),
            auxInp(0)      => stg1CicDorB,

            datOut         => stg1ShfDatB,
            auxOut(0)      => stg1ShfDorB
         );

      P_MULT : process ( filClk ) is
      begin
         if ( rising_edge( filClk ) ) then
            mulbA <= lparms.scale( lparms.scale'left downto lparms.scale'left - mulbA'length + 1 );
            mulbB <= lparms.scale( lparms.scale'left downto lparms.scale'left - mulbB'length + 1 );
            if ( wrDecm = '1' ) then
               if ( rWr.decmIs1 ) then
                  mulaA      <= resize(stg0DatA, mulaA'length);
                  mulaB      <= resize(stg0DatB, mulaB'length);

                  mulDorDlyA <= mulDorDlyA(mulDorDlyA'left - 1 downto 0) & stg0ShfDorA;
                  mulTrgDlyA <= mulTrgDlyA(mulTrgDlyA'left - 1 downto 0) & stg0ShfTrgA;
                  mulDorDlyB <= mulDorDlyB(mulDorDlyB'left - 1 downto 0) & stg0ShfDorB;
               else
                  mulaA      <= signed(stg1DatA(mulaA'range));
                  mulaB      <= signed(stg1DatB(mulaB'range));

                  mulDorDlyA <= mulDorDlyA(mulDorDlyA'left - 1 downto 0) & stg1ShfDorA;
                  mulTrgDlyA <= mulTrgDlyA(mulTrgDlyA'left - 1 downto 0) & stg1ShfTrgA;
                  mulDorDlyB <= mulDorDlyB(mulDorDlyB'left - 1 downto 0) & stg1ShfDorB;
               end if;
               mulpA <= mulaA * mulbA;
               mulpB <= mulaB * mulbB;

            end if;
         end if;
      end process P_MULT;

      stg1DatA <= stg1ShfDatA(stg1DatA'range);
      stg1DatB <= stg1ShfDatB(stg1DatB'range);

      wdatAin  <= std_logic_vector( resize( shift_right( mulpA, mulbA'length - 2 ), wdatAin'length ) );
      wdatBin  <= std_logic_vector( resize( shift_right( mulpB, mulbB'length - 2 ), wdatBin'length ) );
      wdorAin  <= mulDorDlyA(mulDorDlyA'left);
      wdorBin  <= mulDorDlyB(mulDorDlyB'left);

      extTrgSynDelayed <= mulTrgDlyA(mulTrgDlyA'left);

      cenCic1 <= '0'                   when rWr.decmIs1 else cenOut0;
      wrDecm  <= cenOut0               when rWr.decmIs1 else cenOut1;

   end generate G_DECIMATORS;

   G_NO_DECIMATORS : if ( DISABLE_DECIMATORS_G ) generate
     signal cenCic1 : std_logic := '1';
     attribute KEEP         of cenCic1 : signal is "TRUE";
     attribute SYN_KEEP     of cenCic1 : signal is true;

     function ladj(constant x :ADCWord) return RamWord is
        variable v : RamWord;
     begin
        if ( x'length > v'length ) then
           v := RamWord(resize(shift_right(signed(x), x'length - v'length), v'length));
        elsif ( x'length < v'length ) then
           v := RamWord(resize(shift_left(signed(x), v'length - x'length), v'length));
        else
           v := RamWord(x);
        end if;
        return v;
     end function ladj;
   begin
         wdatAin <= ladj(fdatA);
         wdatBin <= ladj(fdatB);
         wdorAin <= fdorA;
         wdorBin <= fdorB;

         extTrgSynDelayed <= extTrgSyn;

         wrDecm  <= '1';

         cenCic1 <= '1';
   end generate G_NO_DECIMATORS;

   statusLoc(ACQ_STA_OVR_A_C) <= toSl( rWr.ovrA /= 0 );
   statusLoc(ACQ_STA_OVR_B_C) <= toSl( rWr.ovrB /= 0 );
   statusLoc(ACQ_STA_HALTD_C) <= memFull;
   statusLoc(ACQ_STA_SRC_A_C) <= toSl( rWr.parms.src = CHA );
   statusLoc(ACQ_STA_SRC_B_C) <= toSl( rWr.parms.src = CHB );

   U_STATUS_SYNC : entity work.SynchronizerBit
      generic map (
         WIDTH_G    => status'length,
         IN_REG_G   => true
      )
      port map (
         clkInp     => memClk,
         datInp     => statusLoc,
         clk        => busClk,
         rst        => busRst,
         datOut     => status
      );

   G_DRAMBUF : if ( USE_SDRAM_BUF_G ) generate

      U_DRAMBUF    : entity work.SampleBufferSDRAM
         generic map (
            A_WIDTH_G   => SDRAM_ADDR_WIDTH_G,
            MEM_DEPTH_G => MEM_DEPTH_G,
            D_WIDTH_G   => (2*RAM_BITS_G)
         )
         port map (
            wrClk       => memClk,
            wrEna       => wrEna,
            wrDat       => wrDat,
            wrFul       => wrFul,

            sdramClk    => sdramClk,
            sdramReq    => sdramReq,
            sdramRep    => sdramRep,

            rdClk       => busClk,
            rdEna       => rdEna,
            rdDat       => rdDat,
            rdEmp       => rdEmp,
            rdFlush     => rRd.flush
         );
   end generate G_DRAMBUF;


   G_BRAMBUF : if ( not USE_SDRAM_BUF_G ) generate

      U_BRAMBUF    : entity work.SampleBufferBRAM
         generic map (
            A_WIDTH_G   => SDRAM_ADDR_WIDTH_G,
            MEM_DEPTH_G => MEM_DEPTH_G,
            D_WIDTH_G   => (2*RAM_BITS_G)
         )
         port map (
            wrClk       => memClk,
            wrEna       => wrEna,
            wrDat       => wrDat,
            wrFul       => wrFul,

            sdramClk    => sdramClk,
            sdramReq    => sdramReq,
            sdramRep    => sdramRep,

            rdClk       => busClk,
            rdEna       => rdEna,
            rdDat       => rdDat,
            rdEmp       => rdEmp,
            rdFlush     => rRd.flush
         );
   end generate G_BRAMBUF;

   err(1) <= rWr.fifoFul;
   err(0) <= rRd.fifoEmp;

end architecture rtl;
