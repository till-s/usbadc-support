library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;
use ieee.math_real.all;

use work.CommandMuxPkg.all;

entity CommandWrapperSim is
end entity CommandWrapperSim;

architecture sim of CommandWrapperSim is

   constant ADC_W_C       : natural := 10;


   constant MEM_DEPTH_C : natural := 1024;
   constant ADC_FIRST_C : unsigned(ADC_W_C - 1 downto 0)  := (others => '1');

   constant BB_SPI_CSb_C  : natural := 0;
   constant BB_SPI_SCK_C  : natural := 1;
   constant BB_SPI_MSO_C  : natural := 2;
   constant BB_SPI_MSI_C  : natural := 3;
   constant BB_SPI_T_C    : natural := 6;


   signal clk     : std_logic := '0';
   signal rst     : std_logic_vector(4 downto 0) := (others => '1');

   signal datIbo  : std_logic_vector(7 downto 0);
   signal vldIbo  : std_logic;
   signal rdyIbo  : std_logic;

   signal datObi  : std_logic_vector(7 downto 0);
   signal vldObi  : std_logic;
   signal rdyObi  : std_logic;

   signal run     : boolean      := true;

   signal bbi     : std_logic_vector(7 downto 0) := x"FF";
   signal bbo     : std_logic_vector(7 downto 0) := x"FF";

   signal adcDDR  : unsigned(ADC_W_C-1 downto 0) := ADC_FIRST_C;
   signal adcA    : unsigned(ADC_W_C-1 downto 0) := ADC_FIRST_C;
   signal adcB    : unsigned(ADC_W_C-1 downto 0) := ADC_FIRST_C;
   signal dorA    : std_logic                    := '0';
   signal dorB    : std_logic                    := '0';
   signal dorDDR  : std_logic                    := '0';

   signal spirReg : std_logic_vector(7 downto 0) := (others => '0');
   signal spirWen : std_logic;
   signal spirRen : std_logic;
   signal spirInp : std_logic_vector(7 downto 0);
   signal spirCsb : std_logic := '1';
   signal spirDO  : std_logic;
   signal spirDI  : std_logic;

   signal sclk    : std_logic;
   signal mosi    : std_logic;
   signal scsb    : std_logic;
   signal shiz    : std_logic;
   signal miso    : std_logic;

   signal subCmdBB: SubCommandBBType;

begin

   P_CLK : process is
   begin
      if ( run ) then
         wait for 10 us;
         clk <= not clk;
      else
         wait;
      end if;
   end process P_CLK;

   U_DRV : entity work.SimPty
      port map (
         clk          => clk,

         vldOb        => vldIbo,
         datOb        => datIbo,
         rdyOb        => rdyIbo,

         vldIb        => vldObi,
         datIb        => datObi,
         rdyIb        => rdyObi
      );

   U_DUT : entity work.CommandWrapper
      generic map (
         FIFO_FREQ_G  => 4.0E5,
         ADC_BITS_G   => ADC_W_C,
         MEM_DEPTH_G  => MEM_DEPTH_C
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

         bbo          => bbo,
         bbi          => bbi,
         subCmdBB     => subCmdBB,

         adcClk       => clk,
         adcRst       => rst(rst'left),

         adcDataDDR(ADC_W_C downto 1)  => std_logic_vector(adcDDR),
         adcDataDDR(               0)  => dorDDR,

         smplClk      => open
      );

   P_RST  : process ( clk ) is
   begin
      if ( rising_edge( clk ) ) then
         rst <= rst(rst'left - 1 downto 0) & '0';
      end if;
   end process P_RST;

   P_FILL_A : process ( clk ) is
   begin
      if ( rising_edge( clk ) ) then
         adcA <= to_unsigned(113, adcA'length); --adcA + 1;
      end if;
   end process P_FILL_A;

   P_FILL_B : process ( clk ) is
      variable i   : natural := 0;
      constant P_C : natural := 1000;
      constant A   : real    := 2.0**real(ADC_W_C - 1) - 1.0;
   begin
      if ( falling_edge( clk ) ) then
         adcB <= unsigned( to_signed( integer( round( A * sin(MATH_2_PI*real(i)/real(P_C)) ) ), adcB'length ) );
         i := i + 1;
         dorB <= '0';
         if ( i = P_C ) then
            i    := 0;
            dorB <= '1';
         end if;
      end if;
   end process P_FILL_B;

   P_DDR : process( clk, adcA, adcB, dorA, dorB ) is
   begin
      if ( clk = '1' ) then
         adcDDR <= adcA;
         dorDDR <= dorA;
      else
         adcDDR <= adcB;
         dorDDR <= dorB;
      end if;
   end process P_DDR;


   P_BBMON : process (bbo) is
   begin
      report "BBO: " & std_logic'image(bbo(1)) & std_logic'image(bbo(0));
   end process P_BBMON;

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

   spirDI   <= mosi   when shiz = '0' else spirDO;
   spirCsb  <= scsb   when subCmdBB = CMD_BB_SPI_FEG_C else '1';
   miso     <= spirDO when subCmdBB = CMD_BB_SPI_FEG_C else 'X';

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

end architecture sim;
