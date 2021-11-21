library ieee;
use     ieee.std_logic_1164.all;
use     ieee.numeric_std.all;
use     ieee.math_real.all;

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

      dcmLocked   : out std_logic := '1';

      dcmRst      : in  std_logic := '0'
   );
end entity MaxADC;

architecture rtl of MaxADC is

   constant NUM_ADDR_BITS_C : natural := numBits(MEM_DEPTH_G - 1);

   constant MS_TICK_PERIOD_C: natural := natural( round( ADC_CLOCK_FREQ_G / 1000.0 ) );

   subtype  RamAddr         is unsigned( NUM_ADDR_BITS_C - 1 downto 0);

   subtype  RamWord         is std_logic_vector(7 downto 0);
   subtype  RamDWord        is std_logic_vector(15 downto 0);

   constant END_ADDR_C      : RamAddr := to_unsigned( MEM_DEPTH_G - 1 , RamAddr'length);

   type     RamArray        is array (MEM_DEPTH_G - 1 downto 0) of RamWord;
   type     RamDArray       is array (MEM_DEPTH_G - 1 downto 0) of RamDWord;

   type     WrStateType     is (FILL, RUN, HOLD);

   type     WrRegType       is record
      state   : WrStateType;
      taddr   : RamAddr;
      lstTrg  : std_logic;
      wasTrg  : boolean;
      nsmpls  : natural range 0 to MEM_DEPTH_G - 1;
      ovrA    : natural range 0 to MEM_DEPTH_G - 1;
      ovrB    : natural range 0 to MEM_DEPTH_G - 1;
      parms   : AcqCtlParmType;
      timer   : unsigned(15 downto 0);
   end record WrRegType;

   constant WR_REG_INIT_C   : WrRegType := (
      state   => FILL,
      taddr   => (others => '0'),
      lstTrg  => '0',
      wasTrg  => false,
      ovrA    => 0,
      ovrB    => 0,
      nsmpls  => 0,
      parms   => ACQ_CTL_PARM_INIT_C,
      timer   => (others => '0')
   );

   type     RdStateType     is (ECHO, HDR, READ);

   type     RdRegType       is record
      state   : RdStateType;
      raddr   : RamAddr;
      rdatA   : RamWord;
      rdatB   : RamWord;
      taddr   : RamAddr;
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
      taddr   => (others => '0'),
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

   signal wdatA     : RamWord;
   signal wdatB     : RamWord;
   signal wdorA     : std_logic;
   signal wdorB     : std_logic;

   signal chnl0DataResynced : std_logic_vector(8 downto 0);

   signal wrDis             : std_logic := '0';
   signal wrDon             : std_logic;
   signal memFull           : std_logic;
   signal rdDon             : std_logic;
   signal wrEna             : boolean;
   signal wrDecm            : std_logic := '1';
   signal extTrgSyncDelayed : std_logic := '0'; -- must be delayed by decimation filter group delay
   signal trg               : std_logic;
   signal startAcq          : std_logic := '0';

   signal acqTglIb          : std_logic;
   signal acqTglOb          : std_logic := '0';

   signal msTick            : boolean;
   signal msTickCounter     : natural range 0 to MS_TICK_PERIOD_C - 1 := MS_TICK_PERIOD_C - 1;

begin

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
            LOCKED             => dcmLocked,
            PSDONE             => dcmPSDone,
            PSCLK              => dcmPSClk,
            PSEN               => '0',
            PSINCDEC           => '0',
            RST                => dcmRst
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
      dcmLocked <= '1';
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

   GEN_TWOMEM_G : if ( not ONE_MEM_G ) generate
      signal DPRAMA    : RamArray;
      signal DPRAMB    : RamArray;
   begin

      P_WR_AB : process ( memClk ) is
      begin
         if ( rising_edge( memClk ) ) then
            chnl0DataResynced               <= chnl0Data;
            if ( wrEna ) then
               DPRAMA( to_integer(waddr) )  <= wdatA;
               DPRAMB( to_integer(waddr) )  <= wdatB;
               if ( waddr = END_ADDR_C ) then
                  waddr                     <= (others => '0');
                  wrDis                     <= '1';
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
            chnl0DataResynced              <= chnl0Data;
            if ( wrEna ) then
               DPRAMD( to_integer(waddr) ) <= wdatB & wdatA;
               if ( waddr = END_ADDR_C ) then
                  waddr                    <= (others => '0');
                  wrDis                    <= '1';
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
      rdatA    <= rdat0;
      rdatB    <= rdat1;
      wdatA    <= chnl0DataResynced(8 downto 1);
      wdorA    <= chnl0DataResynced(         0);
      wdatB    <= chnl1Data        (8 downto 1);
      wdorB    <= chnl1Data        (         0);
   end generate GEN_DDR_A_FIRST;

   GEN_DDR_B_FIRST : if ( not DDR_A_FIRST_G ) generate
      rdatA    <= rdat1;
      rdatB    <= rdat0;
      wdatA    <= chnl1Data        (8 downto 1);
      wdorA    <= chnl1Data        (         0);
      wdatB    <= chnl0DataResynced(8 downto 1);
      wdorB    <= chnl0DataResynced(         0);
   end generate GEN_DDR_B_FIRST;

   -- ise doesn't seem to properly handle nested records
   -- (getting warning about rRd.busOb missing from sensitivity list)
   P_RD_COMB : process (rRd, rRd.busOb, busIb, rdyOb, rdatA, rdatB, wrDon, wdorA, wdorB) is
      variable v : RdRegType;
   begin
      v     := rRd;

      rdyIb <= '1'; -- drop anything extra;

      busOb <= rRd.busOb;

      if ( wrDon = '0' ) then
         v.rdDon := '0';
      end if;

      case ( rRd.state ) is
         when ECHO =>
            v.raddr := (others => '0');
            v.anb   := true;

            busOb     <= busIb;
            busOb.lst <= '0';

            rdyIb   <= rdyOb;
            if ( (rdyOb and busIb.vld) = '1' ) then
               if ( ( wrDon = '1' ) and ( CMD_ACQ_READ_C = subCommandAcqGet( busIb.dat ) ) ) then
                  v.state     := HDR;
                  if ( NUM_ADDR_BITS_C > 7 ) then
                     v.busOb.dat                := std_logic_vector(rRd.taddr(7 downto 0));
                  else
                     v.busOb.dat                := (others => '0');
                     v.busOb.dat(rRd.taddr'range) := std_logic_vector(rRd.taddr);
                  end if;
                  v.busOb.vld := '1';
                  v.busOb.lst := '0';
                  v.lstDly    := '0';
               else
                  busOb.lst <= '1';
                  if ( wrDon = '1' ) then
                    -- implies CMD_ACQ_FLUSH_C = subCommandAcqGet( busIb.dat )
                     v.rdDon:= '1';
                  end if;
               end if;
            end if;

         when HDR  =>
            if ( rdyOb = '1' ) then -- busOb.vld is '1' at this point
               v.anb := not rRd.anb;
               if ( rRd.anb ) then
                  v.busOb.dat := (others => '0');
                  if ( NUM_ADDR_BITS_C > 7 ) then
                     v.busOb.dat(NUM_ADDR_BITS_C - 9 downto 0) := std_logic_vector(rRd.taddr(rRd.taddr'left downto 8));
                  end if;
                  -- prefetch/register
                  v.rdatA := rdatA;
                  v.rdatB := rdatB;
                  v.raddr := rRd.raddr + 1;
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
                  v.rdatA     := rdatA;
                  v.rdatB     := rdatB;
                  if ( rRd.raddr = END_ADDR_C ) then
                     v.lstDly    := '1';
                     v.busOb.lst := rRd.lstDly;
                  else
                     v.raddr     := rRd.raddr + 1;
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

   GEN_MEM_ILA : if ( true ) generate
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

   P_TRG : process ( rWr, wdatA, wdatB, extTrgSyncDelayed ) is
      variable v : std_logic;
      variable l : signed(wdatA'range);
   begin
      l := rWr.parms.lvl(rWr.parms.lvl'left downto rWr.parms.lvl'left - wdatA'length + 1);
      v := '0';
      case ( rWr.parms.src ) is
         when CHA =>
            if ( signed(wdatA) >= l ) then
               v := '1';
            end if;
         when CHB =>
            if ( signed(wdatB) >= l ) then
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

      trg <= v;
   end process P_TRG;

   msTick <= (msTickCounter = 0);

   P_TICK : process ( memClk ) is
   begin
      if ( rising_edge( memClk ) ) then
         if ( msTick ) then
            msTickCounter <= MS_TICK_PERIOD_C - 1;
         else
            msTickCounter <= msTickCounter - 1;
         end if;
      end if;
   end process P_TICK;

   P_WR_COMB : process ( rWr, trg, waddr, rdDon, msTick, wdorA, wdorB ) is
      variable v : WrRegType;
   begin

      v        := rWr;
      v.nsmpls := rWr.nsmpls + 1;
      v.lstTrg := trg;

      -- remember overrange 'seen' during the last MEM_DEPTH_G samples
      if ( wdorA = '1' ) then
         v.ovrA := MEM_DEPTH_G - 1;
      elsif ( rWr.ovrA /= 0 ) then
         v.ovrA := rWr.ovrA - 1;
      end if;
      if ( wdorB = '1' ) then
         v.ovrB := MEM_DEPTH_G - 1;
      elsif ( rWr.ovrB /= 0 ) then
         v.ovrB := rWr.ovrB - 1;
      end if;

      case ( rWr.state ) is

         when FILL       => 
            if ( rWr.nsmpls >= rWr.parms.nprets ) then
               v.state  := RUN;
               v.nsmpls := rWr.nsmpls; -- discard oldest sample (need 1 to fill 'lstTrg')
               if ( rWr.parms.autoTimeMs /= 0 ) then
                  v.timer := rWr.parms.autoTimeMs;
               end if;
            end if;

         when RUN        => 
            if ( msTick and ( rWr.timer /= unsigned(to_signed(-1, rWr.timer'length)) ) ) then
               v.timer := rWr.timer - 1;
            end if;
            if ( ( not rWr.lstTrg and trg ) = '1' or ( rWr.timer = 0 ) ) then
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
               v.nsmpls := 0;
               v.state  := FILL;
               v.wasTrg := false;
               v.timer  := to_unsigned( 0, v.timer'length );
               v.ovrA   := 0;
               v.ovrB   := 0;
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
            rWr       <= WR_REG_INIT_C;
            rWr.parms <= parms;
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

   smplClk <= memClk;

end architecture rtl;
