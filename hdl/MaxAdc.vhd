library ieee;
use     ieee.std_logic_1164.all;
use     ieee.numeric_std.all;

use     work.CommandMuxPkg.all;
use     work.ILAWrapperPkg.all;

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


   subtype  RamAddr   is unsigned( NUM_ADDR_BITS_C - 1 downto 0);

   subtype  RamWord   is std_logic_vector(8 downto 0);
   subtype  RamDWord  is std_logic_vector(17 downto 0);

   constant END_ADDR_C      : RamAddr := to_unsigned( MEM_DEPTH_G - 1 , RamAddr'length);

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
      rdDon   : std_logic;
   end record RegType;

   constant REG_INIT_C : RegType := (
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

   signal r         : RegType    := REG_INIT_C;
   signal rin       : RegType;

   signal chnl0Data : std_logic_vector(8 downto 0);
   signal chnl1Data : std_logic_vector(8 downto 0);

   signal chnl0ClkL : std_logic;
   signal chnl1ClkL : std_logic;

   signal dcmPSDone : std_logic;
   signal dcmStatus : std_logic_vector(7 downto 0);
   signal dcmPSClk  : std_logic := '0';

   signal memClk    : std_logic;

   signal rdatA     : RamWord;
   signal rdatB     : RamWord;
   signal rdat0     : RamWord;
   signal rdat1     : RamWord;

   signal chnl0DataResynced : std_logic_vector(8 downto 0);

   signal wrDis             : std_logic := '0';
   signal wrDon             : std_logic;
   signal rdDon             : std_logic;

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


   U_WR_SYNC : entity work.SynchronizerBit
      port map (
         clk       => busClk,
         rst       => '0',
         datInp(0) => wrDis,
         datOut(0) => wrDon
      );

   U_RD_SYNC : entity work.SynchronizerBit
      port map (
         clk       => memClk,
         rst       => '0',
         datInp(0) => r.rdDon,
         datOut(0) => rdDon
      );

   GEN_TWOMEM_G : if ( not ONE_MEM_G ) generate
      signal DPRAM0    : RamArray;
      signal DPRAM1    : RamArray;

      signal waddrA    : RamAddr := (others => '0');
      signal waddrB    : RamAddr := (others => '0');
   begin

      waddrB <= waddrA;

      P_WR_AB : process ( memClk ) is
      begin
         if ( rising_edge( memClk ) ) then
            chnl0DataResynced               <= chnl0Data;
            if ( wrDis = '0' ) then
               DPRAM0( to_integer(waddrA) ) <= chnl0DataResynced;
               DPRAM1( to_integer(waddrB) ) <= chnl1Data;
               if ( waddrA = END_ADDR_C ) then
                  waddrA                    <= (others => '0');
                  wrDis                     <= '1';
               else
                  waddrA                    <= waddrA + 1;
               end if;
            elsif ( rdDon = '1' ) then
               wrDis <= '0';
            end if;
         end if;
      end process P_WR_AB;

      rdat0 <= DPRAM0(to_integer(r.raddr));
      rdat1 <= DPRAM1(to_integer(r.raddr));

   end generate GEN_TWOMEM_G;

   GEN_ONEMEM_G : if ( ONE_MEM_G ) generate
      signal DPRAMD    : RamDArray;
      signal waddr     : RamAddr := (others => '0');
   begin

      P_WR_AB : process ( memClk ) is
      begin
         if ( rising_edge( memClk ) ) then
            chnl0DataResynced              <= chnl0Data;
            if ( wrDis = '0' ) then
               DPRAMD( to_integer(waddr) ) <= chnl1Data & chnl0DataResynced;
               if ( waddr = END_ADDR_C ) then
                  waddr                    <= (others => '0');
                  wrDis                    <= '1';
               else
                  waddr                    <= waddr + 1;
               end if;
            elsif ( rdDon = '1' ) then
               wrDis <= '0';
            end if;
         end if;
      end process P_WR_AB;

      rdat0 <= DPRAMD( to_integer( r.raddr ) )( 8 downto  0);
      rdat1 <= DPRAMD( to_integer( r.raddr ) )(17 downto  9);

   end generate GEN_ONEMEM_G;

   GEN_DDR_A_FIRST : if ( DDR_A_FIRST_G ) generate
      rdatA    <= rdat0;
      rdatB    <= rdat1;
   end generate GEN_DDR_A_FIRST;

   GEN_DDR_B_FIRST : if ( not DDR_A_FIRST_G ) generate
      rdatA    <= rdat1;
      rdatB    <= rdat0;
   end generate GEN_DDR_B_FIRST;

   -- ise doesn't seem to properly handle nested records
   -- (getting warning about r.busOb missing from sensitivity list)
   P_RD_COMB : process (r, r.busOb, busIb, rdyOb, rdatA, rdatB, wrDon) is
      variable v : RegType;
   begin
      v     := r;

      rdyIb <= '1'; -- drop anything extra;

      busOb <= r.busOb;

      if ( wrDon = '0' ) then
         v.rdDon := '0';
      end if;

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
                     v.rdDon := '1';
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

   GEN_BUS_ILA : if ( false ) generate
      signal bTrg3 : std_logic_vector(7 downto 0);
   begin
      bTrg3(0)          <= '1' when r.anb else '0';
      bTrg3(1)          <= rdyOb;
      bTrg3(2)          <= r.busOb.vld;
      bTrg3(4 downto 3) <= std_logic_vector( to_unsigned( StateType'pos( r.state ), 2 ) );
      bTrg3(7 downto 5) <= r.busOb.dat(7 downto 5);

      U_ILA_REG : component ILAWrapper
         port map (
            clk  => busClk,
            trg0 => r.rdatA(8 downto 1),
            trg1 => r.rdatB(8 downto 1),
            trg2 => std_logic_vector( r.raddr(7 downto 0) ),
            trg3 => bTrg3
         );
   end generate GEN_BUS_ILA;

   smplClk <= memClk;

end architecture rtl;
