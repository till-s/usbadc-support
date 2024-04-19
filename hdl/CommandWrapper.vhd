library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

use work.BasicPkg.all;
use work.CommandMuxPkg.all;
use work.AcqCtlPkg.all;
use work.SDRAMPkg.all;

entity CommandWrapper is
   generic (
      I2C_SCL_G                : integer := -1;        -- index of I2C SCL (to handle clock stretching)
      BBO_INIT_G               : std_logic_vector(7 downto 0) := x"FF";
      I2C_FREQ_G               : real    := 100.0E3;
      FIFO_FREQ_G              : real;
      SPI_FREQ_G               : real    := 10.0E6;
      -- time from CS assertion to first SPI clock (0 -> 1/2 SPI clock)
      SPI_CSLO_NS_G            : real    := 0.0;
      -- min time from CS is held deasserted (0 -> 1/2 SPI clock)
      SPI_CSHI_NS_G            : real    := 0.0;
      -- delay CS deassertion after last SPI clock negedge (0 -> no delay)
      SPI_CSHI_DELAY_NS_G      : real    := 0.0;
      ADC_FREQ_G               : real    := 130.0E6;
      ADC_BITS_G               : natural := 8;
      RAM_BITS_G               : natural := 8;
      MEM_DEPTH_G              : natural := 1024;
      SDRAM_ADDR_WIDTH_G       : natural := 0;
      USE_SDRAM_BUF_G          : boolean := false;
      COMMA_G                  : std_logic_vector(7 downto 0) := x"CA";
      ESCAP_G                  : std_logic_vector(7 downto 0) := x"55";
      DISABLE_DECIMATORS_G     : boolean := false;
      GIT_VERSION_G            : std_logic_vector(31 downto 0) := x"0000_0000";
      BOARD_VERSION_G          : std_logic_vector( 7 downto 0) := x"00";
      -- may configure specific delays for individual SPI
      -- devices (indexed by BB subCmd)
      BB_DELAY_ARRAY_G         : NaturalArray := NATURAL_ARRAY_EMPTY_C;
      -- dedicated SPI interface for faster flash operations;
      HAVE_SPI_CMD_G           : boolean := true;
      HAVE_BB_CMD_G            : boolean := true;
      HAVE_REG_CMD_G           : boolean := true;
      -- registers are in other, asynchronous clock domain
      REG_ASYNC_G              : boolean := false
   );
   port (
      clk          : in  std_logic;
      rst          : in  std_logic;

      datIb        : in  std_logic_vector(7 downto 0);
      vldIb        : in  std_logic;
      rdyIb        : out std_logic;

      datOb        : out std_logic_vector(7 downto 0);
      vldOb        : out std_logic;
      rdyOb        : in  std_logic;

      abrt         : in  std_logic := '0';
      abrtDon      : out std_logic := '0';

      bbo          : out std_logic_vector(7 downto 0);
      bbi          : in  std_logic_vector(7 downto 0) := (others => '0');
      subCmdBB     : out SubCommandBBType;

      adcStatus    : out std_logic_vector(7 downto 0) := (others => '0');
      err          : out std_logic_vector(1 downto 0);

      -- register interface
      regClk       : in  std_logic := '0'; -- only used if REG_ASYNC_G
      regRDat      : in  std_logic_vector(7 downto 0) := (others => '0');
      regWDat      : out std_logic_vector(7 downto 0) := (others => '0');
      regAddr      : out unsigned(7 downto 0)         := (others => '0');
      regRdnw      : out std_logic := '1';
      regVld       : out std_logic := '0';
      regRdy       : in  std_logic := '1';
      regErr       : in  std_logic := '1';


      spiSClk      : out std_logic;
      spiMOSI      : out std_logic;
      spiCSb       : out std_logic;
      spiMISO      : in  std_logic := '0';

      adcClk       : in  std_logic;
      adcRst       : in  std_logic := '0';
      -- bit 0 is the DOR (overrange) bit
      adcDataA     : in  std_logic_vector(ADC_BITS_G downto 0);
      adcDataB     : in  std_logic_vector(ADC_BITS_G downto 0);

      extTrg       : in  std_logic := '0';

      -- SDRAM interface (if SDRAM sample buffer is used)
      sdramClk    : in  std_logic := '0';
      sdramReq    : out SDRAMReqType := SDRAM_REQ_INIT_C;
      sdramRep    : in  SDRAMRepType := SDRAM_REP_INIT_C
   );
end entity CommandWrapper;

architecture rtl of CommandWrapper is

   constant CMD_VER_IDX_C     : natural := 0;
   constant CMD_BB_IDX_C      : natural := 1;
   constant CMD_ADC_MEM_IDX_C : natural := 2;
   constant CMD_ACQ_PRM_IDX_C : natural := 3;
   constant CMD_SPI_IDX_C     : natural := 4;
   constant CMD_REG_IDX_C     : natural := 5;

   constant CMDS_SUPPORTED_C  : CmdsSupportedType := (
      CMD_VER_IDX_C           => true,
      CMD_BB_IDX_C            => HAVE_BB_CMD_G,
      CMD_ADC_MEM_IDX_C       => true,
      CMD_ACQ_PRM_IDX_C       => true,
      CMD_SPI_IDX_C           => HAVE_SPI_CMD_G,
      CMD_REG_IDX_C           => HAVE_REG_CMD_G
   );

   constant NUM_CMDS_C        : natural := CMDS_SUPPORTED_C'length;

   signal   bussesIb          : SimpleBusMstArray(NUM_CMDS_C - 1 downto 0) := (others => SIMPLE_BUS_MST_INIT_C);
   signal   readysIb          : std_logic_vector (NUM_CMDS_C - 1 downto 0) := (others => '1'                  );
   signal   bussesOb          : SimpleBusMstArray(NUM_CMDS_C - 1 downto 0) := (others => SIMPLE_BUS_MST_INIT_C);
   signal   readysOb          : std_logic_vector (NUM_CMDS_C - 1 downto 0) := (others => '1'                  );

   signal   unstuffedBusIb    : SimpleBusMstType                           := SIMPLE_BUS_MST_INIT_C;
   signal   unstuffedRdyIb    : std_logic                                  := '1';
   signal   unstuffedBusOb    : SimpleBusMstType                           := SIMPLE_BUS_MST_INIT_C;
   signal   unstuffedRdyOb    : std_logic                                  := '1';

   signal   deStufferSynced   : std_logic;
   signal   deStufferAbort    : std_logic;

   signal   acqParms          : AcqCtlParmType := ACQ_CTL_PARM_INIT_C;
   signal   acqParmsTgl       : std_logic      := '0';
   signal   acqParmsAck       : std_logic;

   constant VERSION_C : Slv8Array := (
      0 => BOARD_VERSION_G,
      1 => CMD_API_VERSION_C,
      2 => GIT_VERSION_G(31 downto 24),
      3 => GIT_VERSION_G(23 downto 16),
      4 => GIT_VERSION_G(15 downto  8),
      5 => GIT_VERSION_G( 7 downto  0)
   );

   signal verAddr     : integer range -1 to VERSION_C'high := -1;
   signal verAddrIn   : integer range -1 to VERSION_C'high;

   signal stuffRst    : std_logic;

begin

   stuffRst <= ( rst or abrt );

   U_DESTUFFER : entity work.ByteDeStuffer
      generic map (
         COMMA_G     => COMMA_G,
         ESCAP_G     => ESCAP_G
      )
      port map (
         clk         => clk,
         rst         => stuffRst,

         datOut      => unstuffedBusIb.dat,
         vldOut      => unstuffedBusIb.vld,
         lstOut      => unstuffedBusIb.lst,
         rdyOut      => unstuffedRdyIb,
         rstOut      => deStufferAbort,
         synOut      => deStufferSynced,

         datInp      => datIb,
         vldInp      => vldIb,
         rdyInp      => rdyIb
      );

   U_STUFFER : entity work.ByteStuffer
      generic map (
         COMMA_G     => COMMA_G,
         ESCAP_G     => ESCAP_G
      )
      port map (
         clk         => clk,
         rst         => stuffRst,

         datInp      => unstuffedBusOb.dat,
         vldInp      => unstuffedBusOb.vld,
         lstInp      => unstuffedBusOb.lst,
         rdyInp      => unstuffedRdyOb,

         datOut      => datOb,
         vldOut      => vldOb,
         rdyOut      => rdyOb
      );


   U_MUXER : entity work.CommandMux
      generic map (
         CMDS_SUPPORTED_G => CMDS_SUPPORTED_C
      )
      port map (
         clk          => clk,
         rst          => rst,

         busIb        => unstuffedBusIb,
         rdyIb        => unstuffedRdyIb,

         busOb        => unstuffedBusOb,
         rdyOb        => unstuffedRdyOb,

         abrt         => abrt,
         abrtDon      => abrtDon,

         busMuxedIb   => bussesIb,
         rdyMuxedIb   => readysIb,

         busMuxedOb   => bussesOb,
         rdyMuxedOb   => readysOb
      );

   P_VERSION_COMB : process ( bussesIb(CMD_VER_IDX_C), readysOb(CMD_VER_IDX_C), verAddr ) is
      variable v : SimpleBusMstType;
   begin
      v         := bussesIb(CMD_VER_IDX_C);
      v.lst     := '0';
      verAddrIn <= verAddr;
      if ( verAddr < 0 ) then
         readysIb(CMD_VER_IDX_C) <= readysOb(CMD_VER_IDX_C);
         bussesOb(CMD_VER_IDX_C) <= v;
      else
         readysIb(CMD_VER_IDX_C)        <= '1';
         bussesOb(CMD_VER_IDX_C).dat    <= VERSION_C( verAddr );
         bussesOb(CMD_VER_IDX_C).vld    <= '1';
         if ( verAddr = VERSION_C'high ) then
            bussesOb(CMD_VER_IDX_C).lst <= '1';
         else
            bussesOb(CMD_VER_IDX_C).lst <= '0';
         end if;
      end if;
      if ( verAddr < 0 ) then
         if ( ( v.vld and readysOb(CMD_VER_IDX_C) ) = '1' ) then
            verAddrIn <= 0;
         end if;
      else
         if ( readysOb(CMD_VER_IDX_C) = '1' ) then
            if ( verAddr = VERSION_C'high ) then
               verAddrIn <= -1;
            else
               verAddrIn <= verAddr + 1;
            end if;
         end if;
      end if;
   end process P_VERSION_COMB;

   P_VERSION_SEQ : process ( clk ) is
   begin
      if ( rising_edge( clk ) ) then
         if ( rst = '1' ) then
            verAddr <= -1;
         else
            verAddr <= verAddrIn;
         end if;
      end if;
   end process P_VERSION_SEQ;

   G_BITBANG : if ( CMDS_SUPPORTED_C(CMD_BB_IDX_C) ) generate

      U_BITBANG : entity work.CommandBitBang
         generic map (
            I2C_SCL_G    => I2C_SCL_G,
            BBO_INIT_G   => BBO_INIT_G,
            I2C_FREQ_G   => I2C_FREQ_G,
            CLOCK_FREQ_G => FIFO_FREQ_G,
            HPER_DELAY_G => BB_DELAY_ARRAY_G
         )
         port map (
            clk          => clk,
            rst          => rst,

            mIb          => bussesIb(CMD_BB_IDX_C),
            rIb          => readysIb(CMD_BB_IDX_C),

            mOb          => bussesOb(CMD_BB_IDX_C),
            rOb          => readysOb(CMD_BB_IDX_C),

            bbi          => bbi,
            bbo          => bbo,
            subCmd       => subCmdBB
         );
   end generate G_BITBANG;

   G_ADC : if ( CMDS_SUPPORTED_C( CMD_ADC_MEM_IDX_C ) ) generate
      U_ADC_BUF : entity work.MaxAdc
         generic map (
            ADC_CLOCK_FREQ_G     => ADC_FREQ_G,
            MEM_DEPTH_G          => MEM_DEPTH_G,
            ADC_BITS_G           => ADC_BITS_G,
            RAM_BITS_G           => RAM_BITS_G,
            DISABLE_DECIMATORS_G => DISABLE_DECIMATORS_G,
            SDRAM_ADDR_WIDTH_G   => SDRAM_ADDR_WIDTH_G,
            USE_SDRAM_BUF_G      => USE_SDRAM_BUF_G
         )
         port map (
            adcClk       => adcClk,
            adcRst       => adcRst,
            adcDataA     => adcDataA,
            adcDataB     => adcDataB,

            sdramClk     => sdramClk,
            sdramReq     => sdramReq,
            sdramRep     => sdramRep,

            busClk       => clk,
            busRst       => rst,

            parms        => acqParms,
            parmsTgl     => acqParmsTgl,
            parmsAck     => acqParmsAck,

            busIb        => bussesIb(CMD_ADC_MEM_IDX_C),
            rdyIb        => readysIb(CMD_ADC_MEM_IDX_C),

            busOb        => bussesOb(CMD_ADC_MEM_IDX_C),
            rdyOb        => readysOb(CMD_ADC_MEM_IDX_C),

            err          => err,
            status       => adcStatus,

            extTrg       => extTrg
         );
   end generate G_ADC;

   G_PARM : if ( CMDS_SUPPORTED_C( CMD_ACQ_PRM_IDX_C ) ) generate
      U_ACQ_PARMS : entity work.CommandAcqParm
         generic map (
            CLOCK_FREQ_G => FIFO_FREQ_G,
            MEM_DEPTH_G  => MEM_DEPTH_G
         )
         port map (
            clk          => clk,
            rst          => rst,

            mIb          => bussesIb(CMD_ACQ_PRM_IDX_C),
            rIb          => readysIb(CMD_ACQ_PRM_IDX_C),

            mOb          => bussesOb(CMD_ACQ_PRM_IDX_C),
            rOb          => readysOb(CMD_ACQ_PRM_IDX_C),

            parmsOb      => acqParms,
            trgOb        => acqParmsTgl,
            ackIb        => acqParmsAck
         );
   end generate G_PARM;

   G_SPI  : if ( CMDS_SUPPORTED_C( CMD_SPI_IDX_C ) ) generate
      U_SPI : entity work.CommandSpi
         generic map (
            CLOCK_FREQ_G => FIFO_FREQ_G,
            SPI_FREQ_G   => SPI_FREQ_G,
            CSLO_NS_G    => SPI_CSLO_NS_G,
            CSHI_NS_G    => SPI_CSHI_NS_G,
            CSDL_NS_G    => SPI_CSHI_DELAY_NS_G
         )
         port map (
            clk          => clk,
            rst          => rst,

            mIb          => bussesIb(CMD_SPI_IDX_C),
            rIb          => readysIb(CMD_SPI_IDX_C),

            mOb          => bussesOb(CMD_SPI_IDX_C),
            rOb          => readysOb(CMD_SPI_IDX_C),

            spiSClk      => spiSClk,
            spiMOSI      => spiMOSI,
            spiCSb       => spiCSb,
            spiMISO      => spiMISO
         );
   end generate G_SPI;

   G_REGS : if ( CMDS_SUPPORTED_C( CMD_REG_IDX_C ) ) generate
      U_REG : entity work.CommandReg
         generic map (
            ASYNC_G      => REG_ASYNC_G
         )
         port map (
            clk          => clk,
            rst          => rst,

            mIb          => bussesIb(CMD_REG_IDX_C),
            rIb          => readysIb(CMD_REG_IDX_C),

            mOb          => bussesOb(CMD_REG_IDX_C),
            rOb          => readysOb(CMD_REG_IDX_C),

            regClk       => regClk,
            rdat         => regRDat,
            wdat         => regWDat,
            addr         => regAddr,
            rdnw         => regRdnw,
            vld          => regVld,
            rdy          => regRdy,
            err          => regErr
         );
    end generate G_REGS;
 
end architecture rtl;
