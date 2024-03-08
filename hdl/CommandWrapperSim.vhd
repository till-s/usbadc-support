library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;
use ieee.math_real.all;

use work.CommandMuxPkg.all;
use work.SDRAMPkg.all;

entity CommandWrapperSim is
end entity CommandWrapperSim;

architecture sim of CommandWrapperSim is

   constant ADC_W_C       : natural :=  8;
   constant RAM_BITS_C    : natural := 10;

   constant RAM_A_WIDTH_C : natural := 12; -- >= log2(MEM_DEPTH_C)
   constant MEM_DEPTH_C   : natural := 2048;
   constant ADC_FIRST_C   : unsigned(ADC_W_C - 1 downto 0)  := (others => '1');

   constant BB_SPI_CSb_C  : natural := 0;
   constant BB_SPI_SCK_C  : natural := 1;
   constant BB_SPI_MSO_C  : natural := 2;
   constant BB_SPI_MSI_C  : natural := 3;
   constant BB_SPI_T_C    : natural := 6;


   signal clk     : std_logic := '0';
   signal adcClk  : std_logic := '0';
   signal ramClk  : std_logic := '0';
   signal rst     : std_logic_vector(4 downto 0) := (others => '1');

   signal datIbo  : std_logic_vector(7 downto 0);
   signal vldIbo  : std_logic;
   signal rdyIbo  : std_logic;

   signal datObi  : std_logic_vector(7 downto 0);
   signal vldObi  : std_logic;
   signal rdyObi  : std_logic;

   signal abrt    : std_logic;
   signal abrtDon : std_logic;

   signal run     : boolean      := true;

   signal bbi     : std_logic_vector(7 downto 0) := x"FF";
   signal bbo     : std_logic_vector(7 downto 0) := x"FF";

   signal adcA    : unsigned(ADC_W_C-1 downto 0) := ADC_FIRST_C;
   signal adcB    : unsigned(ADC_W_C-1 downto 0) := ADC_FIRST_C;
   signal dorA    : std_logic                    := '0';
   signal dorB    : std_logic                    := '0';

   signal spirReg : std_logic_vector(7 downto 0) := (others => '0');
   signal spirWen : std_logic;
   signal spirRen : std_logic;
   signal spirInp : std_logic_vector(7 downto 0);
   signal spirCsb : std_logic := '1';
   signal spirDO  : std_logic;
   signal spirDI  : std_logic;
   signal memCSb  : std_logic;
   signal memMiso : std_logic;
   signal memSClk : std_logic;
   signal memMosi : std_logic;

   signal ramReq  : SDRAMReqType := SDRAM_REQ_INIT_C;
   signal ramRep  : SDRAMRepType := SDRAM_REP_INIT_C;

   signal sclk    : std_logic;
   signal mosi    : std_logic;
   signal scsb    : std_logic;
   signal shiz    : std_logic;
   signal miso    : std_logic;

   signal spiMISO : std_logic := '0';
   signal spiMOSI : std_logic;
   signal spiSClk : std_logic;
   signal spiCSb  : std_logic;

   signal extTrg  : std_logic := '0';

   signal subCmdBB: SubCommandBBType;

   signal regRDat  : std_logic_vector(7 downto 0) := (others => '0');
   signal regWDat  : std_logic_vector(7 downto 0);
   signal regAddr  : unsigned(7 downto 0);
   signal regRdnw  : std_logic;
   signal regVld   : std_logic;
   signal regRdy   : std_logic := '0';
   signal regErr   : std_logic := '1';

   component RamEmul is
      generic (
         A_WIDTH_G : natural;
         REF_BRK_G : natural
      );
      port (
         clk       : in  std_logic;
         req       : in  std_logic;
         rdnwr     : in  std_logic;
         addr      : in  std_logic_vector(A_WIDTH_G - 1 downto 0);
         ack       : out std_logic;
         vld       : out std_logic;
         wdat      : in  std_logic_vector(15 downto 0);
         rdat      : out std_logic_vector(15 downto 0)
      );
   end component RamEmul;


begin

   P_CLK : process is
   begin
      if ( run ) then
         wait for 8.33 ns;
         clk <= not clk;
      else
         wait;
      end if;
   end process P_CLK;

   P_ADC_CLK : process is
   begin
      if ( run ) then
         wait for 4 ns;
         adcClk <= not adcClk;
      else
         wait;
      end if;
   end process P_ADC_CLK;

   P_RAM_CLK : process is
   begin
      if ( run ) then
         wait for 3 ns;
         ramClk <= not ramClk;
      else
         wait;
      end if;
   end process P_RAM_CLK;


   U_DRV : entity work.SimPty
      port map (
         clk          => clk,

         vldOb        => vldIbo,
         datOb        => datIbo,
         rdyOb        => rdyIbo,

         vldIb        => vldObi,
         datIb        => datObi,
         rdyIb        => rdyObi,

         abrt         => abrt,
         abrtDon      => abrtDon
      );

   U_RAM : component RamEmul
      generic map (
         A_WIDTH_G    => RAM_A_WIDTH_C,
         REF_BRK_G    => 1000
      )
      port map (
         clk          => ramClk,
         req          => ramReq.req,
         rdnwr        => ramReq.rdnwr,
         addr         => ramReq.addr(RAM_A_WIDTH_C - 1 downto 0),
         ack          => ramRep.ack,
         vld          => ramRep.vld,
         wdat         => ramReq.wdat,
         rdat         => ramRep.rdat
      );

   U_DUT : entity work.CommandWrapper
      generic map (
         FIFO_FREQ_G         => 4.0E5,
         SPI_FREQ_G          => 1.0E5,
         ADC_BITS_G          => ADC_W_C,
         RAM_BITS_G          => RAM_BITS_C,
         MEM_DEPTH_G         => MEM_DEPTH_C,
         SDRAM_ADDR_WIDTH_G  => RAM_A_WIDTH_C,
         HAVE_SPI_CMD_G      => true
      )
      port map (
         clk          => clk,
         rst          => rst(rst'left),

         datIb        => datIbo,
         vldIb        => vldIbo,
         rdyIb        => rdyIbo,

         datOb        => datObi,
         vldOb        => vldObi,
         rdyOb        => rdyObi,

         abrt         => abrt,
         abrtDon      => abrtDon,

         bbo          => bbo,
         bbi          => bbi,
         subCmdBB     => subCmdBB,

         regRDat      => regRDat,
         regWDat      => regWDat,
         regAddr      => regAddr,
         regRdnw      => regRdnw,
         regVld       => regVld,
         regRdy       => regRdy,
         regErr       => regErr,

         adcClk       => adcClk,
         adcRst       => rst(rst'left),

         adcDataA(ADC_W_C downto 1)  => std_logic_vector(adcA),
         adcDataA(               0)  => dorA,
         adcDataB(ADC_W_C downto 1)  => std_logic_vector(adcB),
         adcDataB(               0)  => dorB,

         extTrg       => extTrg,

         spiSClk      => spiSClk,
         spiMOSI      => spiMOSI,
         spiMISO      => spiMISO,
         spiCSb       => spiCSb,

         sdramClk     => ramClk,
         sdramReq     => ramReq,
         sdramRep     => ramRep
      );

   P_RST  : process ( clk ) is
   begin
      if ( rising_edge( clk ) ) then
         rst <= rst(rst'left - 1 downto 0) & '0';
      end if;
   end process P_RST;

   P_FILL_A : process ( adcClk ) is
   begin
      if ( rising_edge( adcClk ) ) then
         adcA <= to_unsigned(113, adcA'length); --adcA + 1;
      end if;
   end process P_FILL_A;

   P_FILL_B : process ( adcClk ) is
      variable i   : natural := 0;
      constant P_C : natural := 1000;
      constant A   : real    := 2.0**real(ADC_W_C - 1) - 1.0;
   begin
      if ( falling_edge( adcClk ) ) then
         adcB <= unsigned( to_signed( integer( round( A * sin(MATH_2_PI*real(i)/real(P_C)) ) ), adcB'length ) );
         adcB <= adcB + 1;
         i := i + 1;
         dorB <= '0';
         if ( i = P_C ) then
            i    := 0;
            dorB <= '1';
         end if;
      end if;
   end process P_FILL_B;

--   P_BBMON : process (bbo) is
--   begin
--      report "BBO: " & std_logic'image(bbo(1)) & std_logic'image(bbo(0));
--   end process P_BBMON;

   U_SPIMEM : entity work.SpiFlashSim
      port map (
         clk        => clk,
         sclk       => memSClk,
         scsb       => memCSb,
         mosi       => memMosi,
         miso       => memMiso
      );

   U_SPIREG : entity work.SpiReg
      port map (
         clk        => clk,
         rst        => rst(rst'left),

         sclk       => sclk,
         scsb       => spirCsb,
         mosi       => spirDI,
         miso       => spirDO,

         data_inp   => spirReg,
         rs         => spirRen,
         data_out   => spirInp,
         ws         => spirWen
      );

   sclk              <= bbo(BB_SPI_SCK_C);
   mosi              <= bbo(BB_SPI_MSO_C);
   scsb              <= bbo(BB_SPI_CSb_C);
   shiz              <= bbo(BB_SPI_T_C  );

   bbi(BB_SPI_SCK_C) <= bbo(BB_SPI_SCK_C);
   bbi(BB_SPI_CSb_C) <= bbo(BB_SPI_CSb_C);
   bbi(BB_SPI_MSO_C) <= bbo(BB_SPI_MSO_C);
   bbi(BB_SPI_T_C  ) <= '0';
   bbi(BB_SPI_MSI_C) <= miso;

   spirDI   <= mosi    when shiz = '0' else spirDO;
   spirCsb  <= scsb    when subCmdBB = CMD_BB_SPI_FEG_C   else '1';
   miso     <= spirDO  when subCmdBB = CMD_BB_SPI_FEG_C   else
               memMiso when subCmdBB = CMD_BB_SPI_ROM_C   else 'X';
   memCsb   <= scsb    when subCmdBB = CMD_BB_SPI_ROM_C   else spiCSb;
   memMosi  <= mosi    when subCmdBB = CMD_BB_SPI_ROM_C   else spiMOSI;
   memSClk  <= sclk    when subCmdBB = CMD_BB_SPI_ROM_C   else spiSClk;
   spiMISO  <= memMiso;

   P_SPIR : process ( clk ) is
   begin
      if ( rising_edge(clk) ) then
         if ( rst(rst'left) = '1' ) then
            spirReg <= (others => '0');
         elsif ( spirWen = '1' ) then
            spirReg <= spirInp;
         end if;
      end if;
   end process P_SPIR;

   U_SPI_CHECK : entity work.SpiChecker
      port map (
         clk    => clk,
         sclk   => sclk,
         scsb   => scsb,
         mosi   => mosi,
         miso   => miso,
         viol   => open
      );

   P_REG  : process ( clk, regAddr, regVld, regRdnw ) is
      variable r0 : std_logic_vector(7 downto 0) := (others => 'X');
      variable r1 : std_logic_vector(7 downto 0) := (others => 'X');
      variable r2 : std_logic_vector(7 downto 0) := (others => 'X');

      variable dly: unsigned(2 downto 0)         := to_unsigned(2, 3);
      variable rdy: std_logic;
   begin
      rdy    := '1';
      regErr <= '0';
      case ( to_integer( regAddr ) ) is
         when 4 | 24  =>
            regRDat <= x"14";
            regErr  <= not regRdnw;
         when 5 | 25  =>
            regRDat <= x"15";
            regErr  <= not regRdnw;
         when 6 | 26  =>
            regRDat <= x"16";
            regErr  <= not regRdnw;

         when 10 | 30 =>
            regRDat <= r0;
         when 11 | 31 =>
            regRDat <= r1;
         when 12 | 32 =>
            regRDat <= r2;
         when others =>
            regRDat <= (others => 'X');
            regErr  <= '1';
      end case;
      if ( to_integer( regAddr ) > 20 ) then
         rdy := dly(dly'left);
      end if;
      if ( rising_edge( clk ) ) then
         if ( regVld = '1' ) then
            if ( rdy = '1' ) then
               if ( regRdnw = '0' ) then
                  if    ( regAddr = 10 or regAddr = 30 ) then
                     r0 := regWDat;
                  elsif ( regAddr = 11 or regAddr = 31 ) then
                     r1 := regWDat;
                  elsif ( regAddr = 12 or regAddr = 32 ) then
                     r2 := regWDat;
                  end if;
               end if;
               dly := to_unsigned(2, dly'length);
            else
               dly := dly - 1;
            end if;
         end if;
      end if;
      regRdy <= rdy;
   end process P_REG;

end architecture sim;
