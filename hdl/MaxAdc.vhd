library ieee;
use     ieee.std_logic_1164.all;
use     ieee.numeric_std.all;
use     ieee.math_real.all;

use     work.BasicPkg.all;
use     work.CommandMuxPkg.all;
use     work.ILAWrapperPkg.all;
use     work.AcqCtlPkg.all;

library unisim;
use     unisim.vcomponents.all;

entity MaxADC is
   generic (
      ADC_CLOCK_FREQ_G : real    := 130.0E6;
      MEM_DEPTH_G      : natural := 1024;
      -- depending on which port the muxed signal is shipped either A or B samples are
      -- first (i.e., on the negative edge preceding the positive edge of the adcClk)
      DDR_A_FIRST_G    : boolean := false;
      ONE_MEM_G        : boolean := false;
      USE_DCM_G        : boolean := false;
      TEST_NO_DDR_G    : boolean := false;
      TEST_NO_BUF_G    : boolean := false
   );
   port (
      adcClk      : in  std_logic;
      adcRst      : in  std_logic;

      -- bit 0 is the DOR (overrange) bit
      adcDataDDR  : in  std_logic_vector(8 downto 0);

      smplClk     : out std_logic;

      busClk      : in  std_logic;
      busRst      : in  std_logic;

      parms       : in  AcqCtlParmType;
      -- toggling 'parmsTgl' initiates a new acquisition (aborting a pending one)
      parmsTgl    : in  std_logic;
      parmsAck    : out std_logic;

      busIb       : in  SimpleBusMstType;
      rdyIb       : out std_logic;

      busOb       : out SimpleBusMstType;
      rdyOb       : in  std_logic;

      pllLocked   : out std_logic := '1';

      pllRst      : in  std_logic := '0'
   );
end entity MaxADC;

architecture rtl of MaxADC is
   attribute KEEP           : string;

   constant NUM_ADDR_BITS_C : natural := numBits(MEM_DEPTH_G - 1);

   constant MS_TICK_PERIOD_C: natural := natural( round( ADC_CLOCK_FREQ_G / 1000.0 ) );

   subtype  RamAddr         is unsigned( NUM_ADDR_BITS_C - 1 downto 0);

   subtype  RamWord         is std_logic_vector(7 downto 0);
   subtype  RamDWord        is std_logic_vector(15 downto 0);

   constant END_ADDR_C      : RamAddr := to_unsigned( MEM_DEPTH_G - 1 , RamAddr'length);

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

   constant MSIZE_INFO_C    : unsigned(15 downto 0) := memInfo;

   type     RamArray        is array (MEM_DEPTH_G - 1 downto 0) of RamWord;
   type     RamDArray       is array (MEM_DEPTH_G - 1 downto 0) of RamDWord;

   type     WrStateType     is (FILL, RUN, HOLD);

   type     WrRegType       is record
      state   : WrStateType;
      taddr   : RamAddr;
      lstTrg  : std_logic;
      wasTrg  : boolean;
      nsmpls  : RamAddr;
      ovrA    : RamAddr;
      ovrB    : RamAddr;
      parms   : AcqCtlParmType;
      timer   : unsigned(15 downto 0);
      expired : boolean;
      decmIs1 : boolean;
   end record WrRegType;

   constant WR_REG_INIT_C   : WrRegType := (
      state   => FILL,
      taddr   => (others => '0'),
      lstTrg  => '0',
      wasTrg  => false,
      ovrA    => (others => '0'),
      ovrB    => (others => '0'),
      nsmpls  => (others => '0'),
      parms   => ACQ_CTL_PARM_INIT_C,
      timer   => (others => '0'),
      expired => false,
      decmIs1 => true
   );

   type     RdStateType     is (ECHO, MSIZE, HDR, READ);

   type     RdRegType       is record
      state   : RdStateType;
      raddr   : RamAddr;
      rdatA   : RamWord;
      rdatB   : RamWord;
      saddr   : RamAddr;
      anb     : boolean;
      busOb   : SimpleBusMstType;
      lstDly  : std_logic;
      rdDon   : std_logic;
   end record RdRegType;

   constant RD_REG_INIT_C : RdRegType := (
      state   => ECHO,
      rdatA   => (others => 'X'),
      rdatB   => (others => 'X'),
      raddr   => (others => '0'),
      saddr   => (others => '0'),
      anb     => true,
      busOb   => SIMPLE_BUS_MST_INIT_C,
      lstDly  => '0',
      rdDon   => '0'
   );

   signal rRd       : RdRegType    := RD_REG_INIT_C;
   signal rinRd     : RdRegType;

   signal rWr       : WrRegType    := WR_REG_INIT_C;
   signal rinWr     : WrRegType;

   signal chnl0Data : std_logic_vector(8 downto 0);
   signal chnl1Data : std_logic_vector(8 downto 0);

   signal chnl0ClkL : std_logic;
   signal chnl1ClkL : std_logic;

   signal dcmPSDone : std_logic;
   signal dcmStatus : std_logic_vector(7 downto 0);
   signal dcmPSClk  : std_logic := '0';

   signal memClk    : std_logic;

   signal waddr     : RamAddr      := (others => '0');

   signal rdatA     : RamWord;
   signal rdatB     : RamWord;
   signal rdat0     : RamWord;
   signal rdat1     : RamWord;

   -- raw ADC Data
   signal fdatA     : RamWord;
   signal fdatB     : RamWord;
   signal fdorA     : std_logic;
   signal fdorB     : std_logic;

   -- data into RAM
   signal wdatA     : RamWord;
   signal wdatB     : RamWord;
   signal wdorA     : std_logic;
   signal wdorB     : std_logic;
   signal trg       : std_logic;
   -- keep data in sync with registered trigger
   signal wdatAin   : RamWord;
   signal wdatBin   : RamWord;
   signal wdorAin   : std_logic;
   signal wdorBin   : std_logic;
   signal trgin     : std_logic;

   signal chnl0DataResynced : std_logic_vector(8 downto 0) := (others => '0');

   signal wrDon             : std_logic;
   signal memFull           : std_logic;
   signal rdDon             : std_logic;
   signal wrEna             : boolean;
   signal wrDecm            : std_logic := '1';
   signal extTrgSyncDelayed : std_logic := '0'; -- must be delayed by decimation filter group delay
   signal startAcq          : std_logic := '0';

   signal acqTglIb          : std_logic;
   signal acqTglOb          : std_logic := '0';

   signal msTick            : boolean   := false;
   signal msTickCounter     : natural range 0 to MS_TICK_PERIOD_C - 1 := MS_TICK_PERIOD_C - 1;

begin

   assert MEM_DEPTH_G mod 1024 = 0 and MEM_DEPTH_G >= 1024 report "Cannot report accurate memory size" severity warning;

   GEN_DCM : if ( USE_DCM_G ) generate
      signal dcmOutClk0   : std_logic;
      signal dcmOutClk180 : std_logic;
   begin

     U_DCM  : component DCM_SP
         generic map (
            CLKIN_DIVIDE_BY_2  => FALSE,
            CLK_FEEDBACK       => "1X",
            CLKIN_PERIOD       => (1.0/ADC_CLOCK_FREQ_G),
            DLL_FREQUENCY_MODE => "LOW",
            DESKEW_ADJUST      => "SOURCE_SYNCHRONOUS",
            CLKOUT_PHASE_SHIFT => "FIXED",
            PHASE_SHIFT        => 0,
            STARTUP_WAIT       => FALSE
         )
         port map (
            CLKIN              => adcClk,
            CLKFB              => chnl0ClkL,
            CLK0               => dcmOutClk0,
            CLK180             => dcmOutClk180,
            LOCKED             => pllLocked,
            PSDONE             => dcmPSDone,
            PSCLK              => dcmPSClk,
            PSEN               => '0',
            PSINCDEC           => '0',
            RST                => pllRst
         );

      U_BUFG_A : BUFG
         port map (
            I  => dcmOutClk0,
            O  => chnl0ClkL
         );

      U_BUFG_B : BUFG
         port map (
            I  => dcmOutClk180,
            O  => chnl1ClkL
         );

   end generate GEN_DCM;

   GEN_NO_DCM : if ( not USE_DCM_G ) generate
      chnl0ClkL <= adcClk;
      chnl1ClkL <= '0';
      pllLocked <= '1';
   end generate GEN_NO_DCM;

      -- The manual doesn't precisely explain timing of the multiplexed mode.
      -- It just says when the muxed signal is on the B port that B-samples are
      -- sent "first" and "followed" by A-samples.
      -- The figure shows (without saying whether this depicts 'port-B' or 'port-A' mode
      --       ----       -----       -----
      --     /     \_____/     \_____/
      --
      --       X An   X Bn  X An+1 X Bn+1
      --
      -- Testing indicates that in 'port-B' mode (muxed signal shipped on port B)
      -- The timing is
      --       ----       -----       -----
      --     /     \_____/     \_____/
      --
      --       X Bn   X An  X Bn+1 X An+1
      --
      -- I.e., a sample pair is latched by first capturing on the negative
      -- edge, then on the positive edge.
      --

      memClk <= chnl0ClkL;

      -- IDDR2 only supports synchronizing into a single output
      -- clock domain from differential inputs. Since we are
      -- single-ended we must run separate channel clocks (180deg.
      -- out of phase).
      GEN_IDDR_BITS : for i in adcDataDDR'range generate
         signal adcDataBuffered : std_logic;
      begin
         GEN_IBUF : if ( not TEST_NO_BUF_G ) generate
            U_IBUF : component IBUF
               generic map (
                  IBUF_DELAY_VALUE => "0",
                  IFD_DELAY_VALUE  => "0"
               )
               port map (
                  I             => adcDataDDR(i),
                  O             => adcDataBuffered
               );
         end generate GEN_IBUF;

         GEN_NO_IBUF : if ( TEST_NO_BUF_G ) generate
            adcDataBuffered <= adcDataDDR(i);
         end generate GEN_NO_IBUF;

         GEN_IDDR : if ( not TEST_NO_DDR_G ) generate
            signal chnl0ClkB : std_logic;
         begin

            chnl0ClkB <= not chnl0ClkL;

            U_IDDR : component IDDR2
               port map (
                  C0            => chnl0ClkB, --chnl1ClkL,
                  C1            => chnl0ClkL,
                  CE            => '1',
                  Q0            => chnl0Data(i),
                  Q1            => chnl1Data(i),
                  D             => adcDataBuffered,
                  S             => '0',
                  R             => '0'
               );
         end generate GEN_IDDR;

         GEN_NO_IDDR : if ( TEST_NO_DDR_G ) generate
            chnl0Data(i) <= adcDataBuffered;
            chnl1Data(i) <= adcDataBuffered;
         end generate GEN_NO_IDDR;

      end generate GEN_IDDR_BITS;

   memFull <= '1' when (rWr.state = HOLD) else '0';

   U_WR_SYNC : entity work.SynchronizerBit
      port map (
         clk       => busClk,
         rst       => '0',
         datInp(0) => memFull,
         datOut(0) => wrDon
      );

   U_RD_SYNC : entity work.SynchronizerBit
      port map (
         clk       => memClk,
         rst       => '0',
         datInp(0) => rRd.rdDon,
         datOut(0) => rdDon
      );

   wrEna <= ( ( (not memFull) and wrDecm ) = '1' );

   P_RESYNC_CH0 : process ( memClk ) is
   begin
      if ( rising_edge( memClk ) ) then
         chnl0DataResynced <= chnl0Data;
      end if;
   end process P_RESYNC_CH0;

   GEN_TWOMEM_G : if ( not ONE_MEM_G ) generate
      signal DPRAMA    : RamArray;
      signal DPRAMB    : RamArray;
   begin

      P_WR_AB : process ( memClk ) is
      begin
         if ( rising_edge( memClk ) ) then
            if ( wrEna ) then
               DPRAMA( to_integer(waddr) )  <= wdatA;
               DPRAMB( to_integer(waddr) )  <= wdatB;
               if ( waddr = END_ADDR_C ) then
                  waddr                     <= (others => '0');
               else
                  waddr                     <= waddr + 1;
               end if;
            end if;
         end if;
      end process P_WR_AB;

      rdatA <= DPRAMA(to_integer(rRd.raddr));
      rdatB <= DPRAMB(to_integer(rRd.raddr));

   end generate GEN_TWOMEM_G;

   GEN_ONEMEM_G : if ( ONE_MEM_G ) generate
      signal DPRAMD    : RamDArray;
   begin

      P_WR_AB : process ( memClk ) is
      begin
         if ( rising_edge( memClk ) ) then
            if ( wrEna ) then
               DPRAMD( to_integer(waddr) ) <= wdatB & wdatA;
               if ( waddr = END_ADDR_C ) then
                  waddr                    <= (others => '0');
               else
                  waddr                    <= waddr + 1;
               end if;
            end if;
         end if;
      end process P_WR_AB;

      rdatA <= DPRAMD( to_integer( rRd.raddr ) )( 8 downto  1);
      rdatB <= DPRAMD( to_integer( rRd.raddr ) )(15 downto  8);

   end generate GEN_ONEMEM_G;

   GEN_DDR_A_FIRST : if ( DDR_A_FIRST_G ) generate
      fdatA    <= chnl0DataResynced(8 downto 1);
      fdorA    <= chnl0DataResynced(         0);
      fdatB    <= chnl1Data        (8 downto 1);
      fdorB    <= chnl1Data        (         0);
   end generate GEN_DDR_A_FIRST;

   GEN_DDR_B_FIRST : if ( not DDR_A_FIRST_G ) generate
      fdatA    <= chnl1Data        (8 downto 1);
      fdorA    <= chnl1Data        (         0);
      fdatB    <= chnl0DataResynced(8 downto 1);
      fdorB    <= chnl0DataResynced(         0);
   end generate GEN_DDR_B_FIRST;

   -- ise doesn't seem to properly handle nested records
   -- (getting warning about rRd.busOb missing from sensitivity list)
   P_RD_COMB : process (rRd, rRd.busOb, busIb, rdyOb, rdatA, rdatB, wrDon, wdorA, wdorB, rWr.taddr) is
      variable v : RdRegType;
   begin
      v     := rRd;

      rdyIb <= '1'; -- drop anything extra;

      busOb <= rRd.busOb;

      if ( wrDon = '0' ) then
         v.rdDon := '0';
      end if;

      -- increment read address; this covers all relevant states
      if ( (rdyOb = '1') and rRd.anb ) then
         if ( rRd.raddr = MEM_DEPTH_G - 1 ) then
            v.raddr := to_unsigned( 0, v.raddr'length );
         else
            v.raddr := rRd.raddr + 1;
         end if;
      end if;

      case ( rRd.state ) is
         when ECHO =>
            v.anb   := true;

            busOb     <= busIb;
            busOb.lst <= '0';

            rdyIb   <= rdyOb;
            if ( (rdyOb and busIb.vld) = '1' ) then
               if ( CMD_ACQ_MSIZE_C = subCommandAcqGet( busIb.dat ) ) then
                  v.state     := MSIZE;
                  v.busOb.dat := std_logic_vector( MSIZE_INFO_C(7 downto 0) );
                  v.busOb.lst := '0';
                  v.busOb.vld := '1';
               elsif ( ( wrDon = '1' ) and ( CMD_ACQ_READ_C = subCommandAcqGet( busIb.dat ) ) ) then
                  v.state     := HDR;
                  -- seed the start address for reading
assert false report "FIXME" severity warning;
                  v.raddr     := rWr.taddr;
                  -- transmit start address -- not really necessary; we keep it for
                  -- debugging purposes and maybe to convey additional info in the
                  -- future... 
                  if ( NUM_ADDR_BITS_C > 7 ) then
                     v.busOb.dat                  := std_logic_vector(rRd.saddr(7 downto 0));
                  else
                     v.busOb.dat                  := (others => '0');
                     v.busOb.dat(rRd.saddr'range) := std_logic_vector(rRd.saddr);
                  end if;
                  v.busOb.dat(0) := toSl(rWr.ovrA /= 0);
                  v.busOb.dat(1) := toSl(rWr.ovrB /= 0);
                  v.busOb.vld := '1';
                  v.busOb.lst := '0';
                  v.lstDly    := '0';
               else
                  busOb.lst <= '1';
                  if ( wrDon = '1' ) then  -- implies CMD_ACQ_FLUSH_C = subCommandAcqGet( busIb.dat )
                     v.rdDon:= '1';
                  end if;
               end if;
            end if;

         when MSIZE =>
            if ( rdyOb = '1' ) then -- busOb.vld is '1' at this point
               v.anb := not rRd.anb;
               if ( rRd.anb ) then
                  v.busOb.dat := std_logic_vector( MSIZE_INFO_C(15 downto 8) );
                  v.busOb.lst := '1';
               else
                  v.busOb.vld := '0';
                  v.state     := ECHO;
               end if;
            end if;

         when HDR  =>
            if ( rdyOb = '1' ) then -- busOb.vld is '1' at this point
               v.anb := not rRd.anb;
               if ( rRd.anb ) then
                  v.busOb.dat := (others => '0');
                  if ( NUM_ADDR_BITS_C > 7 ) then
                     v.busOb.dat(NUM_ADDR_BITS_C - 9 downto 0) := std_logic_vector(rRd.saddr(rRd.saddr'left downto 8));
                  end if;
                  -- prefetch/register (raddr is incremented; see above)
                  v.rdatA := rdatA;
                  v.rdatB := rdatB;
               else
                  v.busOb.dat := rRd.rdatA(rRd.rdatA'left downto rRd.rdatA'left - v.busOb.dat'length + 1);
                  v.state     := READ;
               end if;
            end if;

         when READ =>
            if ( rdyOb = '1' ) then -- busOb.vld  is '1' at this point
               v.anb := not rRd.anb;
               if ( rRd.anb ) then
                  v.busOb.dat := rRd.rdatB(rRd.rdatB'left downto rRd.rdatB'left - v.busOb.dat'length + 1);
                  -- prefetch/register (raddr is incremented; see above)
                  v.rdatA     := rdatA;
                  v.rdatB     := rdatB;
                  -- is the end reached (raddr wrapped around to saddr; mem[saddr] prefetched
                  -- again but ignored...)
                  if ( rRd.raddr = rWr.taddr ) then
                     v.lstDly    := '1';
                     v.busOb.lst := rRd.lstDly;
                  end if;
               else
                  if ( rRd.busOb.lst = '1' ) then
                     v.state := ECHO;
                     v.rdDon := '1';
                  end if;
                  v.busOb.dat := rRd.rdatA(rRd.rdatA'left downto rRd.rdatA'left - v.busOb.dat'length + 1);
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
            trg0 => chnl0DataResynced(8 downto 1),
            trg1 => chnl1Data(8 downto 1),
            trg2 => x"00",
            trg3 => x"00"
         );
   end generate GEN_MEM_ILA;

   -- compare the un-delayed wdatAin/wdatBin to produce the registered
   -- trigger 'trg'...
   P_TRG : process ( rWr, wdatAin, wdatBin, extTrgSyncDelayed ) is
      variable v : std_logic;
      variable l : signed(wdatAin'range);
   begin
      l := rWr.parms.lvl(rWr.parms.lvl'left downto rWr.parms.lvl'left - wdatAin'length + 1);
      v := '0';
      case ( rWr.parms.src ) is
         when CHA =>
            if ( signed(wdatAin) >= l ) then
               v := '1';
            end if;
         when CHB =>
            if ( signed(wdatBin) >= l ) then
               v := '1';
            end if;
         when EXT =>
            v := extTrgSyncDelayed;
         when others => -- manual
            -- handle separately
      end case;
      if ( not rWr.parms.rising ) then
         v :=  not v;
      end if;

      trgin <= v;
   end process P_TRG;

   -- register trigger and delay data to remain in-sync
   P_TRG_SEQ : process ( adcClk ) is
   begin
      if ( rising_edge( adcClk ) ) then
         if ( wrDecm = '1' ) then
            trg   <= trgin;
            wdatA <= wdatAin;
            wdatB <= wdatBin;
            wdorA <= wdorAin;
            wdorB <= wdorBin;
         end if;
      end if;
   end process P_TRG_SEQ;


   P_TICK : process ( memClk ) is
   begin
      if ( rising_edge( memClk ) ) then
         if ( msTickCounter = 0 ) then
            msTickCounter <= MS_TICK_PERIOD_C - 1;
            msTick        <= true;
         else
            msTickCounter <= msTickCounter - 1;
            msTick        <= false;
         end if;
      end if;
   end process P_TICK;

   P_WR_COMB : process ( rWr, rWr.parms, trg, waddr, rdDon, msTick, wdorA, wdorB ) is
      variable v : WrRegType;
   begin

      v         := rWr;
      v.lstTrg  := trg;
      v.nsmpls  := rWr.nsmpls + 1;
      v.expired := ( rWr.timer = 0 );

      -- remember overrange 'seen' during the last MEM_DEPTH_G samples
      if ( wdorA = '1' ) then
         v.ovrA := to_unsigned( MEM_DEPTH_G - 1, v.ovrA'length );
      elsif ( rWr.ovrA /= 0 ) then
         v.ovrA := rWr.ovrA - 1;
      end if;
      if ( wdorB = '1' ) then
         v.ovrB := to_unsigned( MEM_DEPTH_G - 1, v.ovrB'length );
      elsif ( rWr.ovrB /= 0 ) then
         v.ovrB := rWr.ovrB - 1;
      end if;


      case ( rWr.state ) is

         when FILL       =>
            if ( rWr.nsmpls >= rWr.parms.nprets ) then
               v.state  := RUN;
               -- discard oldest sample (need 1 to fill 'lstTrg')
               v.nsmpls := resize( rWr.parms.nprets, v.nsmpls'length );
               v.timer  := rWr.parms.autoTimeMs;
            end if;

         when RUN        =>
            if ( msTick and ( rWr.timer /= AUTO_TIME_STOP_C ) ) then
               v.timer := rWr.timer - 1;
            end if;
            if ( not rWr.wasTrg and ( ( ( not rWr.lstTrg and trg ) = '1' ) or rWr.expired ) ) then
               v.wasTrg := true;
               v.taddr  := waddr;
            end if;
            if ( v.wasTrg ) then
               -- compare to MEM_DEPTH_G - 1; this last sample still
               -- is stored and in 'HOLD' state rWr.nsmpls = MEM_DEPTH_G
               if ( MEM_DEPTH_G - 1 = rWr.nsmpls ) then
                  v.state := HOLD;
               end if;
            else
               -- discard oldest sample
               v.nsmpls := rWr.nsmpls;
            end if;

         when HOLD       =>
            -- hold the overrange detector
            v.ovrA   := rWr.ovrA;
            v.ovrB   := rWr.ovrB;
            -- hold the sample counter and other state
            -- that is possibly read by the readout process
            v.nsmpls := rWr.nsmpls;
            v.lstTrg := rWr.lstTrg;
            if ( rdDon = '1' ) then
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
      port map (
         clk       => memClk,
         rst       => '0',
         datInp(0) => parmsTgl,
         datOut(0) => acqTglIb
      );

   U_RD_SYNC_ACQ : entity work.SynchronizerBit
      port map (
         clk       => busClk,
         rst       => '0',
         datInp(0) => acqTglOb,
         datOut(0) => parmsAck
      );

   startAcq <= acqTglIb xor acqTglOb;

   P_WR_SEQ : process ( memClk ) is
   begin
      if ( rising_edge( memClk ) ) then
         acqTglOb <= acqTglIb;
         if ( startAcq = '1' ) then
            rWr         <= WR_REG_INIT_C;
            rWr.parms   <= parms;
            rWr.decmIs1 <= (parms.decm0 = 0);
            if ( parms.nprets > MEM_DEPTH_G - 1 ) then
               rWr.parms.nprets <= to_unsigned( MEM_DEPTH_G - 1, rWr.parms.nprets'length );
            end if;
         elsif ( wrDecm = '1' ) then
            rWr <= rinWr;
         end if;
      end if;
   end process P_WR_SEQ;

   GEN_BUS_ILA : if ( false ) generate
      signal bTrg3 : std_logic_vector(7 downto 0);
   begin
      bTrg3(0)          <= '1' when rRd.anb else '0';
      bTrg3(1)          <= rdyOb;
      bTrg3(2)          <= rRd.busOb.vld;
      bTrg3(4 downto 3) <= std_logic_vector( to_unsigned( RdStateType'pos( rRd.state ), 2 ) );
      bTrg3(7 downto 5) <= rRd.busOb.dat(7 downto 5);

      U_ILA_REG : component ILAWrapper
         port map (
            clk  => busClk,
            trg0 => rRd.rdatA(7 downto 0),
            trg1 => rRd.rdatB(7 downto 0),
            trg2 => std_logic_vector( rRd.raddr(7 downto 0) ),
            trg3 => bTrg3
         );
   end generate GEN_BUS_ILA;

   B_DECIMATORS : block
      -- at least one extra bit; because we cannot scale
      -- precisely with just a right-shift.
      constant STG0_OBITS_C         : natural := fdatA'length + 1;

      constant STG0_STGS_C          : natural := 5;
      constant STG0_LD_MAX_DCM_C    : natural := 4;

      constant STG0_W_C             : natural := fdatA'length + STG0_LD_MAX_DCM_C*STG0_STGS_C;

      constant STG1_OBITS_C         : natural := fdatA'length + 1; -- FIXME; multiplier width
      constant STG1_STGS_C          : natural := 4;
      constant STG1_LD_MAX_DCM_C    : natural :=12;

      constant STG1_W_C             : natural := STG0_OBITS_C + STG1_LD_MAX_DCM_C*STG1_STGS_C;


      signal   stg0Ctl              : std_logic_vector(STG0_STGS_C downto 0);

      signal   cenOut0              : std_logic;

      
      signal   cenCic1              : std_logic;
      attribute KEEP of cenCic1     : signal is "TRUE";

      signal   stg0CicDatA          : signed(STG0_W_C - 1 downto 0);
      signal   stg0CicDorA          : std_logic;
      signal   stg0ShfDatA          : std_logic_vector(STG0_W_C - 1 downto 0);
      signal   stg0ShfDorA          : std_logic;

      signal   stg0CicDatB          : signed(STG0_W_C - 1 downto 0);
      signal   stg0CicDorB          : std_logic;
      signal   stg0ShfDatB          : std_logic_vector(STG0_W_C - 1 downto 0);
      signal   stg0ShfDorB          : std_logic;

      signal   stg0DatA             : std_logic_vector( STG0_OBITS_C - 1 downto 0 );
      signal   stg0DatB             : std_logic_vector( STG0_OBITS_C - 1 downto 0 );

      signal   stg1Ctl              : std_logic_vector(STG1_STGS_C downto 0);

      signal   cenOut1              : std_logic;

      signal   stg1CicDatA          : signed(STG1_W_C - 1 downto 0);
      signal   stg1CicDorA          : std_logic;
      signal   stg1ShfDatA          : std_logic_vector(STG1_W_C - 1 downto 0);
      signal   stg1ShfDorA          : std_logic;

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
      signal   mulDorDlyB           : std_logic_vector(1 downto 0)        := (others => '0');

   begin

      U_CIC0_A : entity work.CicFilter
         generic map (
            DATA_WIDTH_G   => fdatA'length,
            LD_MAX_DCM_G   => STG0_LD_MAX_DCM_C,
            NUM_STAGES_G   => STG0_STGS_C,
            DCM_MASTER_G   => true
         )
         port map (
            clk            => adcClk,
            rst            => adcRst,

            decmInp        => rWr.parms.decm0,

            cenbOut        => stg0Ctl,
            cenbInp        => open,

            dataInp        => signed(fdatA),
            dovrInp        => fdorA,

            dataOut        => stg0CicDatA,
            dovrOut        => stg0CicDorA,

            strbOut        => cenOut0
         );

      U_SHF0_A : entity work.PipelinedRShifter
         generic map (
            DATW_G         => STG0_W_C,
            AUXW_G         => 1,
            SIGN_EXTEND_G  => true,
            PIPL_SHIFT_G   => false
         )
         port map (
            clk            => adcClk,
            rst            => adcRst,
            cen            => cenOut0,

            shift          => std_logic_vector( rWr.parms.shift0 ),

            datInp         => std_logic_vector( stg0CicDatA ),
            auxInp(0)      => stg0CicDorA,

            datOut         => stg0ShfDatA,
            auxOut(0)      => stg0ShfDorA
         );

      stg0DatA <= stg0ShfDatA( stg0DatA'range );

      U_CIC0_B : entity work.CicFilter
         generic map (
            DATA_WIDTH_G   => fdatB'length,
            LD_MAX_DCM_G   => STG0_LD_MAX_DCM_C,
            NUM_STAGES_G   => STG0_STGS_C,
            DCM_MASTER_G   => false
         )
         port map (
            clk            => adcClk,
            rst            => adcRst,

            decmInp        => rWr.parms.decm0,

            cenbOut        => open,
            cenbInp        => stg0Ctl,

            dataInp        => signed(fdatB),
            dovrInp        => fdorB,

            dataOut        => stg0CicDatB,
            dovrOut        => stg0CicDorB,

            strbOut        => open
         );

      U_SHF0_B : entity work.PipelinedRShifter
         generic map (
            DATW_G         => STG0_W_C,
            AUXW_G         => 1,
            SIGN_EXTEND_G  => true,
            PIPL_SHIFT_G   => false
         )
         port map (
            clk            => adcClk,
            rst            => adcRst,
            cen            => cenOut0,

            shift          => std_logic_vector( rWr.parms.shift0 ),

            datInp         => std_logic_vector( stg0CicDatB ),
            auxInp(0)      => stg0CicDorB,

            datOut         => stg0ShfDatB,
            auxOut(0)      => stg0ShfDorB
         );

      stg0DatB <= stg0ShfDatB( stg0DatB'range );

      U_CIC1_A : entity work.CicFilter
         generic map (
            DATA_WIDTH_G   => stg0DatA'length,
            LD_MAX_DCM_G   => STG1_LD_MAX_DCM_C,
            NUM_STAGES_G   => STG1_STGS_C,
            DCM_MASTER_G   => true
         )
         port map (
            clk            => adcClk,
            rst            => adcRst,

            cen            => cenCic1,

            decmInp        => rWr.parms.decm1(STG1_LD_MAX_DCM_C - 1 downto 0),

            cenbOut        => stg1Ctl,
            cenbInp        => open,

            dataInp        => signed(stg0DatA),
            dovrInp        => stg0ShfDorA,

            dataOut        => stg1CicDatA,
            dovrOut        => stg1CicDorA,

            strbOut        => cenOut1
         );

      U_SHF1_A : entity work.PipelinedRShifter
         generic map (
            DATW_G         => STG1_W_C,
            AUXW_G         => 1,
            SIGN_EXTEND_G  => true,
            PIPL_SHIFT_G   => false
         )
         port map (
            clk            => adcClk,
            rst            => adcRst,
            cen            => cenOut1,

            shift          => std_logic_vector( rWr.parms.shift1( numBits(STG1_W_C) - 1 downto 0 ) ),

            datInp         => std_logic_vector( stg1CicDatA ),
            auxInp(0)      => stg1CicDorA,

            datOut         => stg1ShfDatA,
            auxOut(0)      => stg1ShfDorA
         );

      U_CIC1_B : entity work.CicFilter
         generic map (
            DATA_WIDTH_G   => stg0DatB'length,
            LD_MAX_DCM_G   => STG1_LD_MAX_DCM_C,
            NUM_STAGES_G   => STG1_STGS_C,
            DCM_MASTER_G   => false
         )
         port map (
            clk            => adcClk,
            rst            => adcRst,

            cen            => cenCic1,

            decmInp        => rWr.parms.decm1(STG1_LD_MAX_DCM_C - 1 downto 0),

            cenbOut        => open,
            cenbInp        => stg1Ctl,

            dataInp        => signed(stg0DatB),
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
            PIPL_SHIFT_G   => false
         )
         port map (
            clk            => adcClk,
            rst            => adcRst,
            cen            => cenOut1,

            shift          => std_logic_vector( rWr.parms.shift1( numBits(STG1_W_C) - 1 downto 0 ) ),

            datInp         => std_logic_vector( stg1CicDatB ),
            auxInp(0)      => stg1CicDorB,

            datOut         => stg1ShfDatB,
            auxOut(0)      => stg1ShfDorB
         );

      P_MULT : process ( adcClk ) is
      begin
         if ( rising_edge( adcClk ) ) then
            mulbA <= rWr.parms.scale( rWr.parms.scale'left downto rwr.parms.scale'left - mulbA'length + 1 );
            mulbB <= rWr.parms.scale( rWr.parms.scale'left downto rwr.parms.scale'left - mulbB'length + 1 );
            if ( wrDecm = '1' ) then
               if ( rWr.decmIs1 ) then
                  mulaA      <= signed(stg0DatA(mulaA'range));
                  mulaB      <= signed(stg0DatB(mulaB'range));

                  mulDorDlyA <= mulDorDlyA(mulDorDlyA'left - 1 downto 0) & stg0ShfDorA;
                  mulDorDlyB <= mulDorDlyB(mulDorDlyB'left - 1 downto 0) & stg0ShfDorB;
               else
                  mulaA      <= signed(stg1DatA(mulaA'range));
                  mulaB      <= signed(stg1DatB(mulaB'range));

                  mulDorDlyA <= mulDorDlyA(mulDorDlyA'left - 1 downto 0) & stg1ShfDorA;
                  mulDorDlyB <= mulDorDlyB(mulDorDlyB'left - 1 downto 0) & stg1ShfDorB;
               end if;
               mulpA <= mulaA * mulbA;
               mulpB <= mulaB * mulbB;

            end if;
         end if;
      end process P_MULT;


      stg1DatA <= stg1ShfDatA(stg1DatA'range);
      stg1DatB <= stg1ShfDatB(stg1DatB'range);

      wdatAin <= std_logic_vector( mulpA(wdatAin'range) );
      wdatBin <= std_logic_vector( mulpB(wdatBin'range) );
      wdorAin <= mulDorDlyA(mulDorDlyA'left);
      wdorBin <= mulDorDlyB(mulDorDlyB'left);

      cenCic1 <= '0'                   when rWr.decmIs1 else cenOut0;
      wrDecm  <= cenOut0               when rWr.decmIs1 else cenOut1;

   end block B_DECIMATORS;

   smplClk <= memClk;

end architecture rtl;
