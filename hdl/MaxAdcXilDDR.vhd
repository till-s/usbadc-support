library ieee;
use     ieee.std_logic_1164.all;
use     ieee.numeric_std.all;
use     ieee.math_real.all;

library unisim;
use     unisim.vcomponents.all;

entity MaxADCXilDDR is
   generic (
      ADC_CLOCK_FREQ_G     : real    := 130.0E6;
      ADC_BITS_G           : natural := 8;
      DLY_REF_MHZ_G        : real    := 0.0E6;
      USE_PLL_G            : string  := "NONE"; -- NONE, DCM
      DDR_TYPE_G           : string  := "IDDR2"; -- NONE, IDDR2 or IDDR
      TEST_NO_BUF_G        : boolean := false;
      -- depending on which port the muxed signal is shipped either A or B samples are
      -- first (i.e., on the negative edge preceding the positive edge of the adcClk)
      DDR_A_FIRST_G        : boolean := false;
      IDELAY_TAPS_G        : natural := 0;
      IODELAY_GROUP_G      : string  := "ADCDDR";
      INVERT_POL_CHA_G     : boolean := false;
      INVERT_POL_CHB_G     : boolean := false
   );
   port (
      -- unbuffered ADC Clock
      adcClkInp   : in  std_logic;
      adcClkOut   : out std_logic;
      -- bit 0 is the DOR (overrange) bit
      adcDataDDR  : in  std_logic_vector(ADC_BITS_G downto 0);
      adcDataA    : out std_logic_vector(ADC_BITS_G downto 0);
      adcDataB    : out std_logic_vector(ADC_BITS_G downto 0);
      pllLocked   : out std_logic := '1';
      pllRst      : in  std_logic := '0';
      dlyRefClk   : in  std_logic := '0'
   );
end entity MaxADCXilDDR;

architecture rtl of MaxADCXilDDR is

   attribute IODELAY_GROUP  : string;
   attribute DONT_TOUCH     : string;

   signal adcClk    : std_logic;
   signal chnl0ClkL : std_logic;
   signal chnl1ClkL : std_logic;

   signal iDDRClk   : std_logic;

   signal dcmPSDone : std_logic;
   signal dcmPSClk  : std_logic := '0';

   signal chnl0Data : std_logic_vector(ADC_BITS_G downto 0);
   signal chnl1Data : std_logic_vector(ADC_BITS_G downto 0);
   signal chnl0DataResynced : std_logic_vector(ADC_BITS_G downto 0) := (others => '0');

begin

   GEN_DCM : if ( USE_PLL_G = "DCM" ) generate
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
            CLKIN              => adcClkInp,
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

   GEN_NO_DCM : if ( USE_PLL_G /= "DCM" ) generate

      U_BUFG : BUFG
         port map (
            I => adcClkInp,
            O => chnl0ClkL
         );

      chnl1ClkL <= '0';

      pllLocked <= '1';

   end generate GEN_NO_DCM;

   GEN_IDDR_BUFS  : if ( DDR_TYPE_G = "IDDR" ) generate
      U_BUFIO : BUFIO port map ( I => adcClkInp, O => iDDRClk );
      U_BUFG  : BUFG  port map ( I => adcClkInp, O => adcClk  );
   end generate GEN_IDDR_BUFS;

   GEN_IDDR2_BUFS  : if ( DDR_TYPE_G /= "IDDR" ) generate
      adcClk    <= chnl0ClkL;
   end generate GEN_IDDR2_BUFS;

   -- IDDR2 only supports synchronizing into a single output
   -- clock domain from differential inputs. Since we are
   -- single-ended we must run separate channel clocks (180deg.
   -- out of phase).
   GEN_IDDR_BITS : for i in adcDataDDR'range generate
      signal adcDataBuffered : std_logic;
   begin
      GEN_IBUF : if ( not TEST_NO_BUF_G ) generate
      begin
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

      GEN_IDDR2 : if ( DDR_TYPE_G = "IDDR2" ) generate
         signal chnl0ClkB : std_logic;
         attribute DONT_TOUCH of U_IDDR : label is "TRUE";
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
      end generate GEN_IDDR2;

      GEN_IDDR : if ( DDR_TYPE_G = "IDDR" ) generate
         attribute DONT_TOUCH     of U_IDDR   : label is "TRUE";
         attribute IODELAY_GROUP  of U_IDLY   : label is IODELAY_GROUP_G;
         signal    adcDataDelayed : std_logic;
      begin

         U_IDLY : component IDELAYE2
            generic map (
               IDELAY_TYPE   => "FIXED",
               DELAY_SRC     => "IDATAIN",
-- test with IDDR only (Most other logic but USB removed)
-- 19; ref 200, speed-1, ADC clock 125MHz    : hold: .05    setup: -0.124
-- 18; ref 200, speed-1, ADC clock 120MHz    : hold: 0.000  setup:  0.092 => PASSED
-- 17; ref 200, speed-1, ADC clock 120MHz    : hold: -.035  setup:  0.184
-- 16; ref 200, speed-1, ADC clock 120MHz    : hold: -.098  setup: -0.124
-- 18; ref 200, speed-2, ADC clock 130MHz    : hold: .248   setup: -0.118
-- 17; ref 200, speed-2, ADC clock 130MHz    : hold: .186   setup: -0.056
-- 16; ref 200, speed-2, ADC clock 130MHz    : hold:+.123   setup: +0.036 => PASSED
               IDELAY_VALUE  => IDELAY_TAPS_G,
               REFCLK_FREQUENCY => DLY_REF_MHZ_G
            )
            port map (
               C             => '0',
               REGRST        => '0',
               LD            => '0',
               CE            => '0',
               INC           => '0',
               CINVCTRL      => '0',
               CNTVALUEIN    => (others => '0'),
               IDATAIN       => adcDataBuffered,
               DATAIN        => '0',
               LDPIPEEN      => '0',
               DATAOUT       => adcDataDelayed,
               CNTVALUEOUT   => open
            );

         -- mimick IDDR2 plus resync register; chnl0Data is latched on the negedge
         U_IDDR : component IDDR
            generic map (
               DDR_CLK_EDGE  => "SAME_EDGE"
            )
            port map (
               C             => iDDRClk,
               CE            => '1',
               Q1            => chnl1Data(i),
               Q2            => chnl0Data(i),
               D             => adcDataDelayed,
               S             => '0',
               R             => '0'
            );
      end generate GEN_IDDR;

      GEN_NO_IDDR : if ( DDR_TYPE_G /= "IDDR" and DDR_TYPE_G /= "IDDR2" ) generate
         chnl0Data(i) <= adcDataBuffered;
         chnl1Data(i) <= adcDataBuffered;
      end generate GEN_NO_IDDR;

   end generate GEN_IDDR_BITS;

   GEN_IDDR_CTRL : if ( DDR_TYPE_G = "IDDR" ) generate
      attribute IODELAY_GROUP  of U_DLYREF : label is IODELAY_GROUP_G;
   begin
      U_DLYREF : IDELAYCTRL
         port map (
            REFCLK => dlyRefClk,
            RST    => '0',
            RDY    => open
         );
   end generate GEN_IDDR_CTRL;

   GEN_RESYNC : if ( DDR_TYPE_G /= "IDDR" ) generate

      P_RESYNC_CH0 : process ( adcClk ) is
      begin
         if ( rising_edge( adcClk ) ) then
            chnl0DataResynced <= chnl0Data;
         end if;
      end process P_RESYNC_CH0;

   end generate GEN_RESYNC;

   GEN_NO_RESYNC : if ( DDR_TYPE_G = "IDDR" ) generate
      chnl0DataResynced <= chnl0Data;
   end generate GEN_NO_RESYNC;


   P_MAP : process ( chnl0DataResynced, chnl1Data ) is
      variable datA : std_logic_vector(ADC_BITS_G - 1 downto 0);
      variable datB : std_logic_vector(ADC_BITS_G - 1 downto 0);
      variable dorA : std_logic;
      variable dorB : std_logic;
   begin
      if ( DDR_A_FIRST_G ) then
         datA    := chnl0DataResynced(ADC_BITS_G downto 1);
         dorA    := chnl0DataResynced(         0);
         datB    := chnl1Data        (ADC_BITS_G downto 1);
         dorB    := chnl1Data        (         0);
      else
         datA    := chnl1Data        (ADC_BITS_G downto 1);
         dorA    := chnl1Data        (         0);
         datB    := chnl0DataResynced(ADC_BITS_G downto 1);
         dorB    := chnl0DataResynced(         0);
      end if;

      if ( INVERT_POL_CHA_G ) then
         datA    := std_logic_vector( - signed( datA ) );
      end if;

      if ( INVERT_POL_CHB_G ) then
         datB    := std_logic_vector( - signed( datB ) );
      end if;

      adcDataA   <= datA & dorA;
      adcDataB   <= datB & dorB;
   end process P_MAP;

   adcClkOut <= adcClk;
end architecture rtl;
