--library ieee;
--use     ieee.std_logic_1164.all;
--use     ieee.numeric_std.all;
--
--entity RamEmul is
--   generic (
--      A_WIDTH_G : natural;
--      RDPIPEL_G : natural := 5
--   );
--   port (
--      clk       : in  std_logic;
--      req       : in  std_logic;
--      rdnwr     : in  std_logic;
--      addr      : in  std_logic_vector(A_WIDTH_G - 1 downto 0);
--      ack       : out std_logic;
--      vld       : out std_logic;
--      wdat      : in  std_logic_vector(15 downto 0);
--      rdat      : out std_logic_vector(15 downto 0)
--   );
--end entity RamEmul;
--
--architecture rtl of RamEmul is
--
--   type RamArray is array ( natural range <> ) of std_logic_vector(15 downto 0);
--
--   signal mem  : RamArray(0 to 2**A_WIDTH_G - 1);
--   signal rpip : RamArray(0 to RDPIPEL_G - 1);
--   signal vpip : std_logic_vector(0 to RDPIPEL_G - 1) := (others => '0');
--
--   signal ackd : signed(4 downto 0) := (others => '1');
--   signal cnt  : natural := 0;
--
--begin
--
--   process ( clk ) is
--   begin
--      if ( rising_edge( clk ) ) then
--
--         vpip(0) <= '0';
--         rpip(0) <= mem( to_integer( unsigned( addr ) ) );
--
--         if ( ackd >= 0 ) then
--            ackd <= ackd - 1;
--         else
--            if ( req = '1' ) then
--               vpip(0) <= rdnwr;
--               if ( rdnwr = '0' ) then
--                  mem( to_integer( unsigned( addr ) ) ) <= wdat;
--               end if;
--            end if;
--         end if;
--
--         if ( cnt = 0 ) then
--            ackd <= to_signed( 5, ackd'length );
--            cnt  <= 13;
--         else
--            cnt  <= cnt - 1;
--         end if;
--
--         for i in 1 to rpip'high loop
--            rpip(i) <= rpip(i-1);
--            vpip(i) <= vpip(i-1);
--         end loop;
--      end if;
--   end process;
--
--   ack  <= ackd(ackd'left);
--   rdat <= rpip(rpip'high);
--   vld  <= vpip(vpip'high);
--
--end architecture rtl;
--
--
library ieee;
use     ieee.std_logic_1164.all;
use     ieee.numeric_std.all;

entity SampleBufferTb is
end entity SampleBufferTb;

architecture sim of SampleBufferTb is
   constant AW_C : natural := 4;

   signal   ramClk   : std_logic := '0';
   signal   ramAddr  : std_logic_vector(AW_C - 1 downto 0);
   signal   ramWDat  : std_logic_vector(15 downto 0);
   signal   ramRDat  : std_logic_vector(15 downto 0);
   signal   ramRdnw  : std_logic;
   signal   ramAck   : std_logic;
   signal   ramReq   : std_logic;
   signal   ramVld   : std_logic;

   signal   wrClk    : std_logic := '0';
   signal   wrEna    : std_logic := '0';
   signal   wrDat    : std_logic_vector(20 downto 0);
   signal   wrFul    : std_logic;

   signal   rdClk    : std_logic := '0';
   signal   rdEna    : std_logic := '0';
   signal   rdDat    : std_logic_vector(20 downto 0);
   signal   rdEmp    : std_logic;

   signal   run      : boolean   := true;

   signal   reader   : boolean   := false;
   signal   writer   : boolean   := false;

   procedure wtick is
   begin
      wait until rising_edge( wrClk );
   end procedure wtick;

   procedure rtick is
   begin
      wait until rising_edge( rdClk );
   end procedure rtick;

   procedure snd(
      signal   d   : out std_logic_vector(20 downto 0);
      signal   e   : out std_logic;
      constant pre : in  natural;
      constant fil : in  natural;
      constant nrd : in  natural 
   ) is
   begin
      e <= '1';
      for i in 1 to fil loop
         d <= '0' & std_logic_vector( to_unsigned( pre + i, d'length - 1 ) );
         wtick;
      end loop;
      d <= '1' & x"0" & std_logic_vector( to_unsigned( nrd, 16 ) );
      wtick;
      d <= '1' & x"0" & x"0000";
      wtick;
      e <= '0';
      wtick;
   end procedure snd;

   procedure rcv(
      signal   e   : out std_logic;
      constant pre : in  natural;
      constant fil : in  natural;
      constant nrd : in  natural
   ) is
   begin
      e <= '1';
      rtick;
      for i in fil - nrd + 1 to fil loop
         while ( rdEmp = '1' ) loop
           rtick;
         end loop;
         assert to_integer( unsigned( rdDat(19 downto 0) ) )  = pre + i report "readback mismatch" severity failure;
         -- xor tests last flag in all cases
         assert ( i < fil ) xor ( rdDat(rdDat'left) = '1' ) report "LAST flag missing" severity failure;
         rtick;
      end loop;
      e  <= '0';
      rtick;
   end procedure rcv;

begin

   process is
   begin
      if ( not run ) then wait; end if;
      wait for 3 ns;
      ramClk <= not ramClk;
   end process;

   process is
   begin
      if ( not run ) then wait; end if;
      wait for 4 ns;
      wrClk <= not wrClk;
   end process;

   process is
   begin
      if ( not run ) then wait; end if;
      wait for 16.6 ns;
      rdClk <= not rdClk;
   end process;

   process ( wrClk ) is
   begin
      if ( rising_edge( wrClk ) ) then
         assert wrFul = '0' report "Write fifo full" severity failure;
      end if;
   end process;

   P_DRV : process is
      procedure flipSync is
      begin
         writer <= not writer;
         wtick;
         while ( writer /= reader ) loop wtick; end loop;
      end procedure flipSync;
   begin
      wtick; wtick;
      for i in 1 to 10 loop
         for j in 1 to i loop
            snd( wrDat, wrEna, 1000, i, j );
            flipSync;
         end loop;
      end loop;
      run <= false;
      wait;
   end process P_DRV;

   P_RD : process is
   begin
      for i in 1 to 10 loop
         for j in 1 to i loop
            rcv( rdEna, 1000, i, j );
            report "I: " & integer'image(i) & ", J: " & integer'image(j) & " PASSED";
            reader <= writer;
            rtick;
         end loop;
      end loop;
      wait;
   end process P_RD;


   U_RAM : entity work.RamEmul
      generic map (
         A_WIDTH_G => AW_C
      )
      port map (
         clk       => ramClk,
         req       => ramReq,
         rdnwr     => ramRdnw,
         addr      => ramAddr,
         ack       => ramAck,
         vld       => ramVld,
         wdat      => ramWDat,
         rdat      => ramRDat
      );

   U_DUT : entity work.SampleBuffer
      generic map (
         A_WIDTH_G => AW_C
      )
      port map (
         wrClk         => wrClk,
         wrEna         => wrEna,
         wrDat         => wrDat,
         wrFul         => wrFul,

         -- sdram interface
         sdramClk      => ramClk,
         sdramReq      => ramReq,
         sdramAck      => ramAck,
         sdramAddr     => ramAddr,
         sdramRdnwr    => ramRdnw,
         sdramWDat     => ramWDat,
         sdramRDat     => ramRDat,
         sdramRVld     => ramVld,

         -- read side
         rdClk         => rdClk,
         rdEna         => rdEna,
         rdDat         => rdDat,
         rdEmp         => rdEmp
      );
  
end architecture sim;
