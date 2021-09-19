library ieee;
use     ieee.std_logic_1164.all;
use     ieee.numeric_std.all;

use     work.CommandMuxPkg.all;

library unisim;
use     unisim.vcomponents.all;

entity MaxADC is
   generic (
      ADC_CLOCK_FREQ_G : real    := 130.0E6;
      MEM_DEPTH_G      : natural := 1024;
      ONE_MEM_G        : boolean := true
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

   constant NO_UNISIM_C     : boolean := false;

   constant NUM_ADDR_BITS_C : natural := numBits(MEM_DEPTH_G - 1);


   subtype  RamAddr   is unsigned( NUM_ADDR_BITS_C - 1 downto 0);

   subtype  RamWord   is std_logic_vector(7 downto 0);
   subtype  RamDWord  is std_logic_vector(15 downto 0);

   constant END_ADDR_C      : RamAddr := to_unsigned( MEM_DEPTH_G mod 2**RamAddr'length, RamAddr'length);

   type     RamArray  is array (MEM_DEPTH_G - 1 downto 0) of RamWord;
   type     RamDArray is array (MEM_DEPTH_G - 1 downto 0) of RamDWord;

   type     StateType is (ECHO, HDR, READ);

   type   RegType   is record
      state   : StateType;
      raddr   : RamAddr;
      rdatA   : RamWord;
      rdatB   : RamWord;
      taddr   : RamAddr;
      anb     : boolean;
      busOb   : SimpleBusMstType;
      lstDly  : std_logic;
   end record RegType;

   constant REG_INIT_C : RegType := (
      state   => ECHO,
      rdatA   => (others => 'X'),
      rdatB   => (others => 'X'),
      raddr   => (others => '0'),
      taddr   => (others => '0'),
      anb     => true,
      busOb   => SIMPLE_BUS_MST_INIT_C,
      lstDly  => '0'
   );

   signal r         : RegType    := REG_INIT_C;
   signal rin       : RegType;

   signal chnlAData : std_logic_vector(7 downto 0);
   signal chnlBData : std_logic_vector(7 downto 0);

   signal chnlAClkL : std_logic;
   signal chnlBClkL : std_logic;

   signal dcmLocked : std_logic;
   signal dcmPSDone : std_logic;
   signal dcmStatus : std_logic_vector(7 downto 0);
   signal dcmPSClk  : std_logic := '0';
   signal dcmRst    : std_Logic := '0';

   signal memClk    : std_logic;

   signal rdatA     : std_logic_vector(7 downto 0);
   signal rdatB     : std_logic_vector(7 downto 0);

   signal chnlADataResynced : std_logic_vector(7 downto 0);

begin

   GEN_CLOCK : if ( not NO_UNISIM_C ) generate
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

      memClk   <= chnlBClkL;

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

   end generate GEN_CLOCK;

   GEN_SIM : if ( NO_UNISIM_C ) generate
      chnlAClkL <= adcClk;
      chnlBClkL <= not adcClk;
      chnlAData <= adcData;
      chnlBData <= adcData;
   end generate;

   GEN_TWOMEM_G : if ( not ONE_MEM_G ) generate
      signal DPRAMA    : RamArray;
      signal DPRAMB    : RamArray;

      signal waddrA    : RamAddr := (others => '0');
      signal waddrB    : RamAddr := (others => '0');
   begin

      waddrB <= waddrA;

      P_WR_AB : process ( memClk ) is
      begin
         if ( rising_edge( memClk ) ) then
            chnlADataResynced            <= chnlAData;
            DPRAMB( to_integer(waddrB) ) <= chnlBData;
            DPRAMA( to_integer(waddrA) ) <= chnlADataResynced;
            if ( waddrA = END_ADDR_C ) then
               waddrA                    <= (others => '0');
            else
               waddrA                       <= waddrA + 1;
            end if;
         end if;
      end process P_WR_AB;

      rdatA <= DPRAMA(to_integer(r.raddr));
      rdatB <= DPRAMB(to_integer(r.raddr));

   end generate GEN_TWOMEM_G;

   GEN_ONEMEM_G : if ( ONE_MEM_G ) generate
      signal DPRAMD    : RamDArray;
      signal waddr     : RamAddr := (others => '0');
   begin

      P_WR_AB : process ( memClk ) is
      begin
         if ( rising_edge( memClk ) ) then
            chnlADataResynced           <= chnlAData;
            DPRAMD( to_integer(waddr) ) <= chnlADataResynced & chnlBData;
            if ( waddr = END_ADDR_C ) then
               waddr                    <= (others => '0');
            else
               waddr                    <= waddr + 1;
            end if;
         end if;

      end process P_WR_AB;

      rdatA <= DPRAMD( to_integer( r.raddr ) )( 7 downto  0);
      rdatB <= DPRAMD( to_integer( r.raddr ) )(15 downto  8);

   end generate GEN_ONEMEM_G;
    
   P_RD_COMB : process (r, busIb, rdyOb, rdatA, rdatB) is
      variable v : RegType;
   begin
      v     := r;

      rdyIb <= '1'; -- drop anything extra;

      busOb <= r.busOb;

      case ( r.state ) is
         when ECHO => 
            v.raddr := (others => '0');
            v.anb   := true;

            busOb     <= busIb;
            busOb.lst <= '0';

            rdyIb   <= rdyOb;
            if ( (rdyOb and busIb.vld) = '1' ) then
               v.state     := HDR;
               if ( NUM_ADDR_BITS_C > 7 ) then
                  v.busOb.dat                := std_logic_vector(r.taddr(7 downto 0));
               else
                  v.busOb.dat                := (others => '0'); 
                  v.busOb.dat(r.taddr'range) := std_logic_vector(r.taddr);
               end if;
               v.busOb.vld := '1';
               v.busOb.lst := '0';
               v.lstDly    := '0';
            end if;

         when HDR  => 
            if ( rdyOb = '1' ) then -- busOb.vld is '1' at this point
               v.anb := not r.anb;
               if ( r.anb ) then
                  v.busOb.dat := (others => '0');
                  if ( NUM_ADDR_BITS_C > 7 ) then
                     v.busOb.dat(NUM_ADDR_BITS_C - 9 downto 0) := std_logic_vector(r.taddr(r.taddr'left downto 8));
                  end if;
                  -- prefetch/register
                  v.rdatA := rdatA;
                  v.rdatB := rdatB;
                  v.raddr := r.raddr + 1;
               else
                  v.busOb.dat := r.rdatA(r.rdatA'left downto r.rdatA'left - v.busOb.dat'length + 1); 
                  v.state     := READ;
               end if;
            end if;
            
         when READ => 
            if ( rdyOb = '1' ) then -- busOb.vld  is '1' at this point
               v.anb := not r.anb;
               if ( r.anb ) then
                  v.busOb.dat := r.rdatB(r.rdatB'left downto r.rdatB'left - v.busOb.dat'length + 1);
                  v.rdatA     := rdatA;
                  v.rdatB     := rdatB;
                  if ( r.raddr = END_ADDR_C ) then
                     v.lstDly    := '1';
                     v.busOb.lst := r.lstDly;
                  else
                     v.raddr     := r.raddr + 1;
                  end if;
               else
                  if ( r.busOb.lst = '1' ) then
                     v.state := ECHO;
                  end if;
                  v.busOb.dat := r.rdatA(r.rdatA'left downto r.rdatA'left - v.busOb.dat'length + 1); 
               end if;
            end if;
      end case;

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
