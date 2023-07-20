library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;
use ieee.math_real.all;

use work.CommandMuxPkg.all;
use work.ILAWrapperPkg.all;

entity CommandSpi is
   generic (
      SPI_FREQ_G   : real    := 1.0E6;
      CLOCK_FREQ_G : real;
      -- min time to hold cs lo and high, respectively.
      -- setting to 0 uses half a clock period.
      -- These parameters are used to stretch longer
      CSLO_NS_G    : real    := 0.0;
      CSHI_NS_G    : real    := 0.0;
      -- time to delay deassertion of CS after the
      -- last negative clock edge; 0.0 means no delay.
      CSDL_NS_G    : real    := 0.0
   );
   port (
      clk          : in  std_logic;
      rst          : in  std_logic;

      mIb          : in  SimpleBusMstType;
      rIb          : out std_logic;

      mOb          : out SimpleBusMstType;
      rOb          : in  std_logic;

      spiSClk      : out std_logic;
      spiMOSI      : out std_logic;
      spiMISO      : in  std_logic;
      spiCSb       : out std_logic
   );
end entity CommandSpi;

architecture rtl of CommandSpi is

   type StateType is (ECHO, CS, FWD);

   type RegType is record
      state         : StateType;
      csb           : std_logic;
      reqVld        : std_logic;
      lstSeen       : std_logic;
      cmd           : SubCommandSPIType;
   end record RegType;

   constant REG_INIT_C : RegType := (
      state         => ECHO,
      csb           => '1',
      reqVld        => '1',
      lstSeen       => '0',
      cmd           => (others => '0')
   );

   function SPI_CSTM_F(t : real) return   natural
   is
      variable x : real;
   begin
      if ( t = 0.0 ) then
         return 0;
      end if;
      return positive( ceil( t * 1.0E-9 * CLOCK_FREQ_G ) );
   end function SPI_CSTM_F;

   constant SPI_HALF_PER_C   : positive := positive(ceil(CLOCK_FREQ_G/SPI_FREQ_G/2.0));

   constant SPI_CSLO_PER_C   : natural  := SPI_CSTM_F( CSLO_NS_G );
   constant SPI_CSHI_PER_C   : natural  := SPI_CSTM_F( CSHI_NS_G );
   constant SPI_CSDL_PER_C   : natural  := SPI_CSTM_F( CSDL_NS_G );

   signal r                  : RegType  := REG_INIT_C;
   signal rin                : RegType;

   signal repDat             : std_logic_vector(7 downto 0);
   signal repVld             : std_logic;
   signal repRdy             : std_logic;

   signal reqVld             : std_logic;
   signal reqRdy             : std_logic;

   signal spiSClkLoc         : std_logic;
   signal spiMOSILoc         : std_logic;
   signal spiCSbLoc          : std_logic;

begin

   spiSClk <= spiSClkLoc;
   spiMOSI <= spiMOSILoc;
   spiCSb  <= spiCSbLoc;

   G_ILA : if ( false ) generate
      signal stateDbg : std_logic_vector(1 downto 0);
   begin

      stateDbg <= std_logic_vector( to_unsigned( StateType'pos( r.state ), stateDbg'length ) );

      U_BB_ILA : component ILAWrapper
         port map (
            clk              => clk,
            trg0(0)          => spiSClkLoc,
            trg0(1)          => spiMOSILoc,
            trg0(2)          => spiMISO,
            trg0(3)          => spiCSbLoc,
            trg0(4)          => reqVld,
            trg0(5)          => reqRdy,
            trg0(6)          => repVld,
            trg0(7)          => repRdy,

            trg1(1 downto 0) => stateDbg,
            trg1(2)          => rOb,
            trg1(3)          => mIb.vld,
            trg1(4)          => mIb.lst,
            trg1(5)          => r.lstSeen,
            trg1(7 downto 6) => r.cmd,

            trg2             => repDat,
            trg3             => mIb.dat
         );
   end generate G_ILA;

   P_COMB : process ( r, mIb, rOb, repDat, repVld, reqRdy ) is
      variable v       : RegType;
   begin
      v := r;

      mOb     <= mIb;

      rIb     <= rOb;
      reqVld  <= '0';
      repRdy  <= '1'; -- drop - just in case

      case ( r.state ) is
         when ECHO =>
            v.lstSeen := '0';
            if ( (rOb and mIb.vld) = '1' ) then
               if ( mIb.lst /= '1' ) then
                  v.state  := FWD;
                  v.csb    := '0';
               end if;
            end if;

         when CS =>
            -- halt in and outbound traffic
            mOb.vld <= '0';
            rIb     <= '0';
            -- mux to our 'reqVld' flag
            reqVld  <= r.reqVld;
            if ( reqRdy = '1' ) then
               v.reqVld := '0';
            end if;
            if ( repVld = '1' ) then
               if ( r.csb = '0' ) then
                  v.state  := FWD;
               else
                  v.state  := ECHO;
               end if;
            end if;

         when FWD  =>
            mOb.dat <= repDat;
            mOb.vld <= repVld;
            mOb.lst <= r.lstSeen;
            repRdy  <= rOb;
            rIb     <= reqRdy;

            if ( r.lstSeen = '0' ) then
               reqVld  <= mIb.vld;
               rIb     <= reqRdy;
            else
               reqVld  <= '0';
               rIb     <= '0'; -- wait until frame is send
            end if;

            if ( (reqRdy and mIb.vld and mIb.lst) = '1' ) then
               v.lstSeen := '1';
            end if;

            if ( ( rOb and repVld and r.lstSeen ) = '1' ) then
               v.state   := CS;
               v.csb     := '1';
               v.reqVld  := '1';
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

   U_SPI : entity work.SpiBitShifter
      generic map (
         WIDTH_G      => 8,
         DIV2_G       => SPI_HALF_PER_C,
         CSLO_G       => SPI_CSLO_PER_C,
         CSHI_G       => SPI_CSHI_PER_C,
         CSDL_G       => SPI_CSDL_PER_C
      )
      port map (
         clk          => clk,
         rst          => rst,

         datInp       => mIb.dat,
         csbInp       => r.csb,
         vldInp       => reqVld,
         rdyInp       => reqRdy,

         datOut       => repDat,
         vldOut       => repVld,
         rdyOut       => repRdy,

         serClk       => spiSClkLoc,
         serCsb       => spiCSbLoc,
         serInp       => spiMISO,
         serOut       => spiMOSILoc
      );

end architecture rtl;
