library ieee;
use     ieee.std_logic_1164.all;
use     ieee.numeric_std.all;

library unisim;
use     unisim.vcomponents.all;

entity MaxADC is
   generic (
      ADC_CLOCK_FREQ_G : real := 130.0E6
   );
   port (
      adcClk      : in  std_logic;
      adcRst      : in  std_logic;

      adcData     : in  std_logic_vector(7 downto 0);

      chnlAClk    : out std_logic;
      chnlBClk    : out std_logic;

      memClk      : in  std_logic;
      memFoo      : out std_logic_vector(15 downto 0)
   );
end entity MaxADC;

architecture rtl of MaxADC is
   signal chnlAData : std_logic_vector(7 downto 0);
   signal chnlBData : std_logic_vector(7 downto 0);

   type   Slv8Array is array (natural range 0 to 1023) of std_logic_vector(7 downto 0);

   signal DPRAMA    : Slv8Array;
   signal DPRAMB    : Slv8Array;

   signal raddrA    : unsigned(9 downto 0) := (others => '0');
   signal waddrA    : unsigned(9 downto 0) := (others => '0');
   signal raddrB    : unsigned(9 downto 0) := (others => '0');
   signal waddrB    : unsigned(9 downto 0) := (others => '0');

   signal cntFoo    : unsigned(15 downto 0) := (others => '0');
   signal inpFoo    : std_logic_vector(15 downto 0);

   signal chnlAClkL : std_logic;
   signal chnlBClkL : std_logic;

   signal dcmLocked : std_logic;
   signal dcmPSDone : std_logic;
   signal dcmStatus : std_logic_vector(7 downto 0);
   signal dcmPSClk  : std_logic := '0';
   signal dcmRst    : std_Logic := '0';

begin

   B_CLOCK : block is
      signal dcmInpClk    : std_logic;
      signal dcmOutClk0   : std_logic;
      signal dcmOutClk180 : std_logic;
   begin

      U_IBUF : component IBUFG
         port map (
            I => adcClk,
            O => dcmInpClk
         );

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
            CLKIN              => dcmInpClk,
            CLKFB              => chnlAClkL,
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
            O  => chnlAClkL
         );

      U_BUFG_B : BUFG
         port map (
            I  => dcmOutClk180,
            O  => chnlBClkL
         );

      chnlAClk <= chnlAClkL;
      chnlBClk <= chnlBClkL;

   end block B_CLOCK;

   -- IDDR2 only supports synchronizing into a single output
   -- clock domain from differential inputs. Since we are
   -- single-ended we must run separate channel clocks (180deg.
   -- out of phase).
   GEN_IDDR : for i in adcData'range generate
      signal adcDataBuffered : std_logic;
   begin
      U_IBUF : component IBUF
         generic map (
            IBUF_DELAY_VALUE => "0",
            IFD_DELAY_VALUE  => "0"
         )
         port map (
            I             => adcData(i),
            O             => adcDataBuffered
         );
      U_IDDR : component IDDR2
         port map (
            C0            => chnlAClkL,
            C1            => not chnlAClkL, --chnlBClkL,
            CE            => '1',
            Q0            => chnlAData(i),
            Q1            => chnlBData(i),
            D             => adcDataBuffered,
            S             => '0',
            R             => '0'
         );
   end generate GEN_IDDR;

   P_WR_A : process ( chnlAClkL ) is
   begin
      if ( rising_edge( chnlAClkL ) ) then
         DPRAMA( to_integer(waddrA) ) <= chnlAData;
         waddrA                       <= waddrA + 1;
      end if;
   end process P_WR_A;

   P_WR_B : process ( chnlBClkL ) is
   begin
      if ( rising_edge( chnlBClkL ) ) then
         DPRAMB( to_integer(waddrB) ) <= chnlBData;

         waddrB <= waddrB + 1;
      end if;
   end process P_WR_B;

   P_RD : process ( memClk ) is
   begin
      if ( rising_edge( memClk ) ) then
         inpFoo(15 downto 8) <= DPRAMA( to_integer(raddrA) );
         inpFoo( 7 downto 0) <= DPRAMB( to_integer(raddrB) );

         raddrA <= raddrA + 1;
         raddrB <= raddrB + 1;

         cntFoo <= cntFoo + unsigned( inpFoo ) + unsigned(chnlAData) + unsigned(chnlBData);
      end if;
   end process P_RD;

   memFoo <= std_logic_vector(cntFoo);

end architecture rtl;
