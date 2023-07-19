library ieee;
use     ieee.std_logic_1164.all;
use     ieee.numeric_std.all;

entity SpiShadowRegTb is
end entity SpiShadowRegTb;

architecture sim of SpiShadowRegTb is
   constant N_C           : natural   := 3;
   constant NREG_C        : natural   := 2;
   constant HPER_C        : natural   := 2; -- half-period of SCLK (in clk ticks)

   type  Slv8Array is array ( natural range <> ) of std_logic_vector(7 downto 0);
   signal clk             : std_logic := '0';
   signal sclk            : std_logic := '0';
   signal scsb            : std_logic_vector(N_C - 1 downto 0) := (others => '1');

   signal rsSrc, wsSrc    : std_logic;

   signal datSrc          : std_logic_vector(7 downto 0) := (others => 'X');
   signal datSnk          : std_logic_vector(7 downto 0) := (others => 'X');

   signal mosi            : std_logic_vector(3 downto 0);
   signal miso            : std_logic_vector(3 downto 0);

   signal run             : boolean   := true;

   signal datChk          : Slv8Array(NREG_C - 1 downto 0);
   signal wsChk           : std_logic_vector(NREG_C - 1 downto 0);

   signal sclkMux         : std_logic_vector(NREG_C - 1 downto 0);
   signal scsbMux         : std_logic_vector(NREG_C - 1 downto 0);

   constant DAT_CMP_C     : Slv8Array(0 to NREG_C - 1) := (
      0 => x"FF",
      1 => x"5A"
   );

   signal wsChecked       : natural := 0;
   signal rbChecked       : natural := 0;

   procedure hpertick is
   begin
      for i in 1 to HPER_C loop
         wait until rising_edge( clk );
      end loop;
   end procedure hpertick;

begin

   P_CLK : process is
   begin
      if ( not run ) then wait; else
         wait for 20 ns;
         clk <= not clk;
      end if;
   end process P_CLK;

   P_FEED : process (rsSrc) is
      variable cnt : natural := 0;
   begin
      datSrc <= (others => 'X');
      if ( rising_edge(rsSrc) ) then
         case ( cnt ) is
            when 0 => datSrc <= x"01";
            when 1 => datSrc <= DAT_CMP_C(1);
            when 2 => -- last RS before CS is asserted
            when 3 => datSrc <= x"81"; cnt := cnt + 1;
            when 4 => datSrc <= x"FF"; cnt := cnt + 1;
            when 5 => -- last RX before CS is asserted
            when 6 => datSrc <= x"80"; cnt := cnt + 1;
            when 7 => datSrc <= x"FF"; cnt := cnt + 1;
            when others =>
               datSrc <= (others => '1');
         end case;
         cnt := cnt + 1;
      end if;
   end process P_FEED;

   P_RDBK : process (wsSrc, datSnk) is
      variable cnt : natural := 0;
   begin
      if ( rising_edge( wsSrc ) ) then
         case cnt is
            when 3 =>
               assert datSnk = DAT_CMP_C(1) report "data readback (1) FAILED" severity failure;
               rbChecked <= rbChecked + 1;

            when 5 =>
               assert datSnk = DAT_CMP_C(0) report "data readback (0) FAILED" severity failure;
               rbChecked <= rbChecked + 1;


            when others =>
         end case;
         cnt := cnt + 1;
      end if;
   end process P_RDBK;

   P_DRV : process is
      variable cnt : natural := 0;
   begin
      hpertick;
      scsb(0) <= '0';
      for i in 1 to 16 loop
         hpertick;
         sclk <= '1';
         hpertick;
         sclk <= '0';
      end loop;
      hpertick;
      scsb(0) <= '1';
      hpertick;
   end process P_DRV;

   U_DRV : entity work.SpiReg
      generic map (
         FRAMED_G => true
      )
      port map (
         clk      => clk,
         sclk     => sclk,
         scsb     => scsb(0),
         mosi     => miso(0),
         miso     => mosi(0),

         data_inp => datSrc,
         rs       => rsSrc,
         ws       => wsSrc,
         data_out => datSnk
      );

   U_DUT : entity work.SpiShadowReg
      generic map (
         NUM_REGS_G => NREG_C,
         REG_INIT_G => (
                         1 => x"ff",
                         0 => x"c6"
                       )
      )
      port map (
         clk      => clk,
         sclkIb   => sclk,
         scsbIb   => scsb(0),
         mosiIb   => mosi(0),
         misoIb   => miso(0),

         sclkOb   => sclkMux,
         scsbOb   => scsbMux
      );

   U_REG_1 : entity work.SpiReg
      generic map (
         FRAMED_G => true
      )
      port map (
         clk      => clk,
         sclk     => sclkMux(1),
         scsb     => scsbMux(1),
         mosi     => mosi(0),
         miso     => open,

         data_inp => x"FF",
         rs       => open,
         ws       => wsChk(1),
         data_out => datChk(1)
      );

   U_REG_0 : entity work.SpiReg
      generic map (
         FRAMED_G => true
      )
      port map (
         clk      => clk,
         sclk     => sclkMux(0),
         scsb     => scsbMux(0),
         mosi     => mosi(0),
         miso     => open,

         data_inp => x"FF",
         rs       => open,
         ws       => wsChk(0),
         data_out => datChk(0)
      );


   P_CHK : process ( wsChk ) is
   begin
      for i in wsChk'range loop
         if ( wsChk(i) = '1' ) then
            assert datChk(i) = DAT_CMP_C(i) report "target data mismatch" severity failure;
            wsChecked <= wsChecked + 1;
         end if;
      end loop;
   end process P_CHK;


   U_CHK : entity work.SpiChecker
      port  map (
         clk      => clk,
         sclk     => sclk,
         scsb     => scsb(0),
         mosi     => mosi(0),
         miso     => miso(0)
      );

   G_CHECKERS : for i in 0 to NREG_C - 1 generate
      U_DUT_CHK : entity work.SpiChecker
         port  map (
            clk      => clk,
            sclk     => sclkMux(i),
            scsb     => scsbMux(i),
            mosi     => mosi(0),
            miso     => '0'
         );
   end generate G_CHECKERS;

   P_WRAPUP : process is
   begin
      wait until ( wsChecked = 1 and rbChecked = 2 );
      report "TEST PASSED";
      run <= false;
      wait;
   end process P_WRAPUP;

end architecture sim;
