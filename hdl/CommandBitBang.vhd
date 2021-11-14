library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;
use ieee.math_real.all;

use work.CommandMuxPkg.all;
use work.ILAWrapperPkg.all;

entity CommandBitBang is
   generic (
      I2C_SCL_G    : integer := -1; -- index of I2C SCL (to handle clock stretching)
      SPI_SCLK_G   : natural range 0 to 7;
      SPI_MOSI_G   : natural range 0 to 7;
      SPI_MISO_G   : natural range 0 to 7;
      BBO_INIT_G   : std_logic_vector(7 downto 0) := x"FF";
      I2C_FREQ_G   : real    := 100.0E3;
      CLOCK_FREQ_G : real
   );
   port (
      clk          : in  std_logic;
      rst          : in  std_logic;

      mIb          : in  SimpleBusMstType;
      rIb          : out std_logic;

      mOb          : out SimpleBusMstType;
      rOb          : in  std_logic;

      subCmd       : out SubCommandBBType;

      bbo          : out std_logic_vector(7 downto 0);
      bbi          : in  std_logic_vector(7 downto 0)
   );
end entity CommandBitBang;

architecture rtl of CommandBitBang is

   type StateType is (ECHO, FWD);

   type RegType is record
      state         : StateType;
      cmd           : SubCommandBBType;
      lstSeen       : std_logic;
   end record RegType;

   constant REG_INIT_C : RegType := (
      state         => ECHO,
      cmd           => (others => '0'),
      lstSeen       => '0'
   );

   signal r               : RegType := REG_INIT_C;
   signal rin             : RegType;

   signal rIbLoc          : std_logic;
   signal mLc             : SimpleBusMstType;

   signal wdatBB          : std_logic_vector(7 downto 0);
   signal wvldBB          : std_logic;
   signal wrdyBB          : std_logic;
   signal wdatSPI         : std_logic_vector(7 downto 0);
   signal wvldSPI         : std_logic;
   signal wrdySPI         : std_logic;

   signal rvldBB          : std_logic;
   signal rvldSPI         : std_logic;
   signal rrdyBB          : std_logic;
   signal rrdySPI         : std_logic;

   signal i2cDis          : std_logic;

   signal sclk            : std_logic;
   signal mosi            : std_logic;
   signal miso            : std_logic := '0';

   signal bboLoc          : std_logic_vector(7 downto 0);

   signal trg0            : std_logic_vector(7 downto 0) := (others => '0');
   signal trg1            : std_logic_vector(7 downto 0) := (others => '0');
   signal trg2            : std_logic_vector(7 downto 0) := (others => '0');
   signal trg3            : std_logic_vector(7 downto 0) := (others => '0');
begin

   subCmd  <= r.cmd;

   trg0(0) <= mIb.vld;
   trg0(1) <= rIbLoc;
   trg0(2) <= rvldBB;
   trg0(3) <= rrdyBB;
   trg0(4) <= rvldSPI;
   trg0(5) <= rrdySPI;
   trg0(6) <= mIb.lst;
   trg0(7 downto 7) <= std_logic_vector(to_unsigned(StateType'pos(r.state),1));

   trg1(0) <= mLc.vld;
   trg1(1) <= rOb;
   trg1(2) <= wvldBB;
   trg1(3) <= wrdyBB;
   trg1(4) <= wvldSPI;
   trg1(5) <= wrdySPI;
   trg1(6) <= mLc.lst;
   trg1(7) <= '0';

   trg2(2 downto 0) <= r.cmd;
   trg2(7 downto 3) <= (others => '0');

   GEN_ILA : if ( true ) generate
   begin
      U_ILA_MEM : component ILAWrapper
         port map (
            clk  => clk,
            trg0 => trg0,
            trg1 => trg1,
            trg2 => trg2,
            trg3 => trg3
         );
   end generate GEN_ILA;


   P_COMB : process ( r, mIb, rOb, wdatBB, wvldBB, wdatSPI, wvldSPI, rrdyBB, rrdySPI ) is
      variable v       : RegType;
      variable rrdy    : std_logic := '0';
      variable wvld    : std_logic := '0';
      variable rvld    : std_logic := '0';
   begin
      v := r;

      mLc     <= SIMPLE_BUS_MST_INIT_C;
      mLc.dat <= mIb.dat;
      mLc.vld <= mIb.vld;
      mLc.lst <= mIb.lst;

      rIbLoc  <= rOb;
      rvldBB  <= '0';
      rvldSPI <= '0';
      wrdyBB  <= '1'; -- drop - just in case
      wrdySPI <= '1'; -- drop - just in case

      if ( r.cmd = CMD_BB_I2C_C ) then
         i2cDis <= '0';
      else
         i2cDis <= '1';
      end if;

      case ( r.state ) is
         when ECHO =>
            v.lstSeen := '0';
            if ( (rOb and mIb.vld) = '1' ) then
               v.cmd := mIb.dat(NUM_CMD_BITS_C + SubCommandBBType'length - 1 downto NUM_CMD_BITS_C);
               if ( mIb.lst /= '1' ) then
                  v.state := FWD;
               end if;
            end if;
         when FWD  =>

            mLc.lst <= r.lstSeen;

            if ( r.cmd = CMD_BB_TEST_C ) then
               rrdy    := rrdySPI;
            else
               rrdy    := rrdyBB;
            end if;

            if ( r.lstSeen = '0' ) then
               rvld    := mIb.vld;
               rIbLoc  <= rrdy;
            else
               rvld    := '0';
               rIbLoc  <= '0'; -- wait until frame is send
            end if;

            if ( r.cmd = CMD_BB_TEST_C ) then
               mLc.dat <= wdatSPI;
               mLc.vld <= wvldSPI;
               wrdySPI <= rOb;
               wvld    := wvldSPI;
               rvldSPI <= rvld;
            else
               mLc.dat <= wdatBB;
               mLc.vld <= wvldBB;
               wrdyBB  <= rOb;
               wvld    := wvldBB;
               rvldBB  <= rvld;
            end if;

            if ( (rrdy and mIb.vld and mIb.lst) = '1' ) then
               v.lstSeen := '1';
            end if;

            if ( ( rOb and wvld and r.lstSeen ) = '1' ) then
               v.state   := ECHO;
               v.lstSeen := '0';
            end if;

      end case;

      rin     <= v;
   end process P_COMB;

   P_SEQ : process ( clk ) is
   begin
      if ( rising_edge( clk ) ) then
         if ( rst = '1' ) then
            r <= REG_INIT_C;
         else
            r <= rin;
         end if;
      end if;
   end process P_SEQ;

   U_BB : entity work.BitBangIF
      generic map (
         I2C_SCL_G    => I2C_SCL_G,
         BBO_INIT_G   => BBO_INIT_G,
         I2C_FREQ_G   => I2C_FREQ_G,
         CLOCK_FREQ_G => CLOCK_FREQ_G
      )
      port map (
         clk          => clk,
         rst          => rst,

         i2cDis       => i2cDis,

         rdat         => mIb.dat,
         rvld         => rvldBB,
         rrdy         => rrdyBB,

         wdat         => wdatBB,
         wvld         => wvldBB,
         wrdy         => wrdyBB,

         bbo          => bboLoc,
         bbi          => bbi
      );

   U_SPI : entity work.WordSpiIF
      generic map (
         SPI_FREQ_G   => CLOCK_FREQ_G,
         CLOCK_FREQ_G => CLOCK_FREQ_G
      )
      port map (
         clk          => clk,
         rst          => rst,

         rdat         => mIb.dat,
         rvld         => rvldSPI,
         rrdy         => rrdySPI,

         wdat         => wdatSPI,
         wvld         => wvldSPI,
         wrdy         => wrdySPI,


         sclk         => sclk,
         mosi         => mosi,
         miso         => miso
      );

   P_SPI_MUX : process ( r, sclk, mosi, bboLoc, bbi ) is
   begin
      bbo <= bboLoc;
      if ( r.cmd = CMD_BB_TEST_C ) then
         bbo(SPI_SCLK_G) <= sclk;
         bbo(SPI_MOSI_G) <= mosi;
      end if;
   end process P_SPI_MUX;

   miso <= bbi(SPI_MISO_G);

   rIb  <= rIbLoc;
   mOb  <= mLc;

end architecture rtl;
