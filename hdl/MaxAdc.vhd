library ieee;
use     ieee.std_logic_1164.all;
use     ieee.numeric_std.all;

use     work.CommandMuxPkg.all;

--SIMlibrary unisim;
--SIMuse     unisim.vcomponents.all;

entity MaxADC is
   generic (
      ADC_CLOCK_FREQ_G : real    := 130.0E6;
      MEM_DEPTH_G      : natural := 1024
   );
   port (
      adcClk      : in  std_logic;
      adcRst      : in  std_logic;

      adcData     : in  std_logic_vector(7 downto 0);

      chnlAClk    : out std_logic;
      chnlBClk    : out std_logic;

      busClk      : in  std_logic;
      busRst      : in  std_logic;

      busIb       : in  SimpleBusMstType;
      rdyIb       : out std_logic;

      busOb       : out SimpleBusMstType;
      rdyOb       : in  std_logic
   );
end entity MaxADC;

architecture rtl of MaxADC is

   constant NO_UNISIM_C     : boolean := true;

   constant NUM_ADDR_BITS_C : natural := numBits(MEM_DEPTH_G - 1);


   subtype  RamAddr   is unsigned( NUM_ADDR_BITS_C - 1 downto 0);

   subtype  RamWord   is std_logic_vector(7 downto 0);

   constant END_ADDR_C      : RamAddr := to_unsigned( MEM_DEPTH_G mod 2**RamAddr'length, RamAddr'length);
   type     RamArray  is array (MEM_DEPTH_G - 1 downto 0) of RamWord;

   type     StateType is (ECHO, HDR, READ);

   type   RegType   is record
      state   : StateType;
      raddr   : RamAddr;
      rdatA   : RamWord;
      rdatB   : RamWord;
      taddr   : RamAddr;
      anb     : boolean;
   end record RegType;

   constant REG_INIT_C : RegType := (
      state   => ECHO,
      rdatA   => (others => 'X'),
      rdatB   => (others => 'X'),
      raddr   => (others => '0'),
      taddr   => (others => '0'),
      anb     => true
   );

   signal r         : RegType    := REG_INIT_C;
   signal rin       : RegType;

   signal chnlAData : std_logic_vector(7 downto 0);
   signal chnlBData : std_logic_vector(7 downto 0);

   signal DPRAMA    : RamArray;
   signal DPRAMB    : RamArray;

   signal waddrA    : RamAddr := (others => '0');
   signal waddrB    : RamAddr := (others => '0');

   signal chnlAClkL : std_logic;
   signal chnlBClkL : std_logic;

   signal dcmLocked : std_logic;
   signal dcmPSDone : std_logic;
   signal dcmStatus : std_logic_vector(7 downto 0);
   signal dcmPSClk  : std_logic := '0';
   signal dcmRst    : std_Logic := '0';

begin

--SIM   GEN_CLOCK : if ( not NO_UNISIM_C ) generate
--SIM      signal dcmInpClk    : std_logic;
--SIM      signal dcmOutClk0   : std_logic;
--SIM      signal dcmOutClk180 : std_logic;
--SIM   begin
--SIM
--SIM      U_IBUF : component IBUFG
--SIM         port map (
--SIM            I => adcClk,
--SIM            O => dcmInpClk
--SIM         );
--SIM
--SIM      U_DCM  : component DCM_SP
--SIM         generic map (
--SIM            CLKIN_DIVIDE_BY_2  => FALSE,
--SIM            CLK_FEEDBACK       => "1X",
--SIM            CLKIN_PERIOD       => (1.0/ADC_CLOCK_FREQ_G),
--SIM            DLL_FREQUENCY_MODE => "LOW",
--SIM            DESKEW_ADJUST      => "SOURCE_SYNCHRONOUS",
--SIM            CLKOUT_PHASE_SHIFT => "FIXED",
--SIM            PHASE_SHIFT        => 0,
--SIM            STARTUP_WAIT       => FALSE
--SIM         )
--SIM         port map (
--SIM            CLKIN              => dcmInpClk,
--SIM            CLKFB              => chnlAClkL,
--SIM            CLK0               => dcmOutClk0,
--SIM            CLK180             => dcmOutClk180,
--SIM            LOCKED             => dcmLocked,
--SIM            PSDONE             => dcmPSDone,
--SIM            PSCLK              => dcmPSClk,
--SIM            PSEN               => '0',
--SIM            PSINCDEC           => '0',
--SIM            RST                => dcmRst
--SIM         );
--SIM
--SIM      U_BUFG_A : BUFG
--SIM         port map (
--SIM            I  => dcmOutClk0,
--SIM            O  => chnlAClkL
--SIM         );
--SIM
--SIM      U_BUFG_B : BUFG
--SIM         port map (
--SIM            I  => dcmOutClk180,
--SIM            O  => chnlBClkL
--SIM         );
--SIM
--SIM      chnlAClk <= chnlAClkL;
--SIM      chnlBClk <= chnlBClkL;
--SIM
--SIM      -- IDDR2 only supports synchronizing into a single output
--SIM      -- clock domain from differential inputs. Since we are
--SIM      -- single-ended we must run separate channel clocks (180deg.
--SIM      -- out of phase).
--SIM      GEN_IDDR : for i in adcData'range generate
--SIM         signal adcDataBuffered : std_logic;
--SIM      begin
--SIM         U_IBUF : component IBUF
--SIM            generic map (
--SIM               IBUF_DELAY_VALUE => "0",
--SIM               IFD_DELAY_VALUE  => "0"
--SIM            )
--SIM            port map (
--SIM               I             => adcData(i),
--SIM               O             => adcDataBuffered
--SIM            );
--SIM         U_IDDR : component IDDR2
--SIM            port map (
--SIM               C0            => chnlAClkL,
--SIM               C1            => not chnlAClkL, --chnlBClkL,
--SIM               CE            => '1',
--SIM               Q0            => chnlAData(i),
--SIM               Q1            => chnlBData(i),
--SIM               D             => adcDataBuffered,
--SIM               S             => '0',
--SIM               R             => '0'
--SIM            );
--SIM      end generate GEN_IDDR;
--SIM
--SIM   end generate GEN_CLOCK;

   GEN_SIM : if ( NO_UNISIM_C ) generate
      chnlAClkL <= adcClk;
      chnlBClkL <= not adcClk;
      chnlAData <= adcData;
      chnlBData <= adcData;
   end generate;


   P_WR_A : process ( chnlAClkL ) is
   begin
      if ( rising_edge( chnlAClkL ) ) then
         if ( to_integer(waddrA) < MEM_DEPTH_G ) then
            DPRAMA( to_integer(waddrA) ) <= chnlAData;
            waddrA                       <= waddrA + 1;
         end if;
      end if;
   end process P_WR_A;

   P_WR_B : process ( chnlBClkL ) is
   begin
      if ( rising_edge( chnlBClkL ) ) then
         if ( to_integer(waddrB) < MEM_DEPTH_G ) then
            DPRAMB( to_integer(waddrB) ) <= chnlBData;
            waddrB <= waddrB + 1;
         end if;
      end if;
   end process P_WR_B;

   P_RD_COMB : process (r, busIb, rdyOb, DPRAMA, DPRAMB) is
      variable v : RegType;
      variable b : SimpleBusMstType;
   begin
      v     := r;

      rdyIb <= '1'; -- drop anything extra;

      busOb <= busIb;
      b     := SIMPLE_BUS_MST_INIT_C;
      busOb.lst <= '0';

      case ( r.state ) is
         when ECHO => 
            v.raddr := (others => '0');
            v.anb   := true;
            b       := busIb;
            b.lst   := '0';
            busOb.lst <= '0';

            rdyIb   <= rdyOb;
            if ( (rdyOb and b.vld) = '1' ) then
               v.state := HDR;
            end if;

         when HDR  => 
            b.vld   := '1';
            busOb.vld <= '1';
            if ( (rdyOb and b.vld) = '1' ) then
               v.anb := not r.anb;
               if ( r.anb ) then
                  if ( NUM_ADDR_BITS_C > 7 ) then
                     b.dat                := std_logic_vector(r.taddr(7 downto 0));
                  else
                     b.dat(r.taddr'range) := std_logic_vector(r.taddr);
                  end if;
                  busOb.dat <= b.dat;
               else
                  if ( NUM_ADDR_BITS_C > 7 ) then
                     b.dat   := std_logic_vector(r.taddr(r.taddr'left downto 8));
busOb.dat <= b.dat;
else
busOb.dat <= (others => '0');
                  end if;
                  -- prefetch/register
                  v.rdatA := DPRAMA(to_integer(r.raddr));
                  v.rdatB := DPRAMB(to_integer(r.raddr));
                  v.raddr := r.raddr + 1;
                  v.state := READ;
               end if;
            end if;
            
         when READ => 
            b.vld   := '1';
busOb.vld <= '1';
            if ( r.anb ) then
               b.dat := r.rdatA(r.rdatA'left downto r.rdatA'left - b.dat'length + 1);
            else
               b.dat := r.rdatB(r.rdatB'left downto r.rdatB'left - b.dat'length + 1);
            end if;
busOb.dat <= b.dat;
            if ( (rdyOb and b.vld) = '1' ) then
               v.anb := not r.anb;
               if ( not r.anb ) then
                  if ( r.raddr = END_ADDR_C ) then
                     b.lst   := '1';
busOb.lst<='1';
                     v.state := ECHO;
                  else
                     v.rdatA := DPRAMA(to_integer(r.raddr));
                     v.rdatB := DPRAMB(to_integer(r.raddr));
                     v.raddr := r.raddr + 1;
                  end if;
               end if;
            end if;
      end case;

--      busOb <= b;
      rin   <= v;
   end process P_RD_COMB;

   P_RD_SEQ : process ( busClk ) is
   begin
      if ( rising_edge( busClk ) ) then
         if ( busRst = '1' ) then
            r <= REG_INIT_C;
         else
            r <= rin;
         end if;
      end if;
   end process P_RD_SEQ;

end architecture rtl;
