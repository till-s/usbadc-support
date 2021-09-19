library ieee;

use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity ByteStufferTb is
end entity ByteStufferTb;

architecture rtl of ByteStufferTb is

   constant COMMA_C : std_logic_vector(7 downto 0) := x"00";
   constant ESCAP_C : std_logic_vector(7 downto 0) := x"01";

   constant EXPECTED_OCTS_C : natural              := 1041;

   type   TestArray is array (natural range <>) of natural range 0 to 255;

   constant testVec : TestArray := (
      58, 248, 85, 225, 243, 121, 155, 51, 41, 204, 33, 173, 217, 124, 42, 151,
      100, 89, 114, 59, 194, 231, 154, 203, 91, 53, 181, 211, 197, 8, 90, 175,
      33, 159, 200, 54, 121, 247, 132, 10, 69, 57, 58, 44, 216, 253, 162, 155,
      232, 38, 108, 129, 75, 102, 243, 65, 205, 64, 146, 131, 29, 162, 116, 58,
      138, 156, 228, 102, 255, 111, 180, 71, 21, 195, 31, 52, 191, 118, 92, 178,
      181, 18, 83, 143, 5, 233, 153, 18, 45, 166, 10, 71, 161, 18, 135, 253,
      157, 85, 246, 154, 31, 209, 35, 174, 155, 154, 229, 204, 20, 171, 204, 30,
      253, 90, 30, 241, 185, 161, 122, 88, 190, 19, 147, 228, 170, 179, 9, 202,
      103, 65, 44, 188, 38, 0, 134, 9, 25, 223, 132, 93, 50, 101, 91, 148,
      46, 124, 35, 193, 145, 127, 24, 109, 236, 172, 59, 48, 215, 100, 50, 198,
      200, 37, 247, 199, 84, 222, 34, 252, 198, 11, 124, 16, 112, 63, 146, 98,
      231, 21, 253, 105, 243, 172, 67, 5, 73, 111, 92, 36, 242, 211, 103, 44,
      6, 188, 215, 117, 175, 139, 217, 211, 208, 29, 191, 151, 119, 235, 59, 68,
      73, 101, 96, 147, 131, 162, 53, 240, 169, 50, 80, 176, 60, 21, 190, 243,
      108, 123, 226, 252, 43, 98, 84, 156, 92, 117, 46, 183, 172, 29, 186, 175,
      189, 129, 52, 88, 33, 75, 188, 252, 59, 220, 100, 176, 48, 88, 162, 109,
      226, 86, 54, 68, 73, 38, 10, 43, 167, 220, 200, 94, 243, 20, 132, 123,
      115, 4, 165, 142, 90, 176, 74, 101, 11, 124, 209, 140, 79, 99, 56, 110,
      88, 136, 64, 63, 91, 119, 57, 253, 178, 70, 138, 150, 20, 92, 176, 202,
      174, 92, 70, 242, 156, 95, 134, 146, 27, 52, 30, 2, 59, 155, 175, 194,
      42, 20, 233, 109, 59, 240, 3, 207, 107, 95, 52, 23, 54, 209, 138, 90,
      208, 191, 36, 2, 213, 163, 114, 44, 64, 111, 159, 54, 119, 91, 75, 5,
      13, 144, 238, 152, 100, 49, 153, 96, 175, 101, 85, 158, 213, 50, 97, 142,
      134, 143, 188, 84, 73, 254, 148, 209, 155, 122, 208, 159, 240, 200, 70, 17,
      60, 0, 193, 171, 172, 77, 6, 210, 134, 224, 174, 0, 251, 228, 137, 204,
      123, 125, 97, 158, 105, 96, 43, 64, 228, 25, 145, 211, 117, 164, 45, 208,
      231, 33, 159, 138, 193, 51, 165, 39, 245, 71, 159, 115, 126, 79, 4, 251,
      87, 34, 172, 78, 151, 175, 238, 183, 142, 32, 37, 28, 137, 155, 134, 44,
      133, 186, 50, 216, 152, 157, 62, 152, 196, 192, 151, 110, 104, 63, 140, 162,
      125, 79, 248, 148, 182, 163, 151, 125, 133, 178, 58, 239, 17, 76, 14, 245,
      230, 70, 212, 226, 173, 25, 111, 23, 0, 121, 196, 10, 152, 119, 194, 230,
      41, 103, 252, 228, 72, 223, 117, 46, 141, 17, 88, 186, 25, 236, 86, 184,
      126, 157, 59, 157, 226, 105, 174, 64, 105, 46, 72, 68, 117, 217, 112, 133,
      195, 230, 124, 138, 95, 131, 252, 206, 208, 161, 52, 189, 160, 74, 63, 100,
      199, 13, 83, 187, 183, 235, 144, 221, 22, 60, 9, 37, 187, 225, 162, 229,
      6, 14, 131, 237, 1, 63, 78, 245, 121, 111, 253, 24, 252, 154, 72, 205,
      31, 167, 237, 201, 154, 10, 107, 226, 124, 237, 1, 69, 20, 241, 222, 27,
      146, 34, 161, 254, 24, 163, 168, 173, 180, 221, 5, 27, 198, 201, 197, 79,
      108, 174, 179, 250, 113, 208, 149, 130, 193, 240, 95, 119, 199, 37, 243, 35,
      7, 223, 2, 6, 222, 124, 145, 192, 46, 171, 142, 226, 70, 33, 160, 110,
      208, 217, 167, 5, 10, 100, 4, 199, 23, 73, 18, 72, 73, 1, 179, 3,
      122, 55, 83, 50, 222, 225, 197, 75, 67, 246, 139, 25, 122, 113, 214, 17,
      108, 163, 248, 83, 54, 77, 166, 149, 96, 228, 110, 21, 41, 217, 34, 140,
      76, 170, 210, 185, 53, 33, 23, 243, 205, 162, 28, 27, 117, 152, 115, 126,
      50, 211, 249, 170, 189, 145, 191, 216, 184, 170, 242, 33, 128, 106, 13, 43,
      203, 145, 236, 86, 57, 222, 225, 217, 194, 100, 129, 95, 52, 216, 14, 247,
      199, 106, 181, 62, 254, 122, 190, 147, 118, 186, 86, 142, 236, 223, 44, 36,
      164, 91, 52, 130, 176, 207, 243, 107, 84, 94, 105, 122, 37, 188, 51, 58,
      169, 194, 207, 112, 192, 122, 15, 53, 129, 115, 166, 232, 72, 247, 37, 160,
      29, 203, 102, 61, 27, 69, 27, 9, 94, 178, 161, 109, 60, 202, 99, 101,
      113, 204, 49, 67, 59, 234, 9, 193, 37, 177, 238, 82, 100, 48, 206, 215,
      55, 24, 138, 142, 165, 126, 195, 205, 56, 91, 159, 3, 193, 229, 169, 232,
      209, 90, 195, 198, 85, 29, 43, 1, 107, 94, 158, 77, 233, 86, 102, 95,
      248, 61, 101, 62, 143, 13, 185, 60, 244, 82, 212, 31, 132, 177, 135, 152,
      254, 89, 36, 210, 111, 227, 147, 151, 74, 241, 17, 25, 82, 180, 107, 234,
      193, 255, 152, 153, 164, 55, 125, 240, 7, 149, 135, 66, 151, 119, 17, 32,
      114, 19, 43, 97, 125, 153, 234, 58, 149, 3, 127, 140, 251, 122, 34, 55,
      147, 248, 245, 165, 233, 117, 78, 228, 92, 60, 161, 220, 174, 43, 99, 7,
      137, 150, 245, 225, 38, 183, 185, 121, 4, 102, 191, 71, 159, 64, 227, 115,
      244, 246, 95, 107, 85, 209, 100, 240, 244, 171, 134, 120, 163, 152, 133, 234,
      238, 190, 129, 9, 241, 38, 123, 63, 1, 145, 184, 24, 113, 66, 37, 67,
      219, 219, 131, 49, 102, 136, 25, 98, 64, 18, 191, 2, 251, 54, 68, 112,
      160, 57, 105, 89, 18, 144, 196, 236, 108, 192, 29, 112, 137, 136, 250, 12,
      62, 28, 231, 209, 190, 246, 143, 37, 53, 196, 252, 41, 165, 24, 98, 194
   );

   signal clk : std_logic := '0';
   signal rst : std_logic := '0';
   signal run : boolean   := true;

   type   EPType is record
      d       : std_logic_vector(7 downto 0);
      v       : std_logic;
      s       : std_logic;
      n       : natural;
      nTxFram : natural;
      nTxOcts : natural;
   end record EPType;

   constant EP_INIT_C : EPType := (
      d       => (others => 'X'),
      v       => '0',
      s       => '0',
      n       =>  0,
      nTxFram =>  0,
      nTxOcts =>  0
   );

   signal xmit   : EPType := EP_INIT_C;

   signal rdyInp : std_logic;
   signal datOut : std_logic_vector(7 downto 0);
   signal vldOut : std_logic;
   signal rdyOut : std_logic;
   signal rdyOutR: std_logic                    := '1';

   signal datOutX: std_logic_vector(7 downto 0);
   signal vldOutX: std_logic;
   signal lstOutX: std_logic;
   signal rdyOutX: std_logic                    := '1';
   signal nRxFram: natural                      := 0;
   signal nRxOcts: natural                      := 0;

   signal rxSyncd: std_logic;
   signal rstTst : std_logic := '0';

   function to_hstring(x : std_logic_vector) return string is
      constant hl : natural := ( x'length + 3 ) / 4;
      variable lx : std_logic_vector(x'left + 3 downto x'right);
      variable s  : string(hl-1 downto 0);
      variable dig: natural;
   begin
      lx := (others => '0');
      lx(x'range) := x;
      for i in hl-1 downto 0 loop
         dig := to_integer( unsigned(lx(x'right + i*4 + 3 downto x'right + i*4 )) );
         case dig is
            when  0 => s(i) := '0';
            when  1 => s(i) := '1';
            when  2 => s(i) := '2';
            when  3 => s(i) := '3';
            when  4 => s(i) := '4';
            when  5 => s(i) := '5';
            when  6 => s(i) := '6';
            when  7 => s(i) := '7';
            when  8 => s(i) := '8';
            when  9 => s(i) := '9';
            when 10 => s(i) := 'A';
            when 11 => s(i) := 'B';
            when 12 => s(i) := 'C';
            when 13 => s(i) := 'D';
            when 14 => s(i) := 'E';
            when 15 => s(i) := 'F';
            when others => s(i) := 'x';
         end case;
      end loop;
      return s;
   end function to_hstring;

   procedure sendCmd(
      signal    ept : inout EPType;
      constant  dat : std_logic_vector(7 downto 0) := (others => 'X');
      constant  lst : std_logic := '0';
      constant  dly : natural   :=  0
   ) is
   begin
     for i in 0 to dly loop
        wait until rising_edge( clk );
     end loop;
     ept.v <= '1';
     ept.d <= dat;
     ept.s <= lst;
     if ( lst = '1' ) then
        ept.n <= ept.n + 1;
     end if;
     wait until rising_edge( clk );
     while ( (ept.v and rdyInp) = '0' ) loop
        wait until rising_edge( clk );
     end loop;
     if ( rxSyncd = '1' ) then
        ept.nTxOcts <= ept.nTxOcts + 1;
        if ( lst = '1' ) then
           ept.nTxFram <= ept.nTxFram + 1;
        end if;
     end if;
     ept.d   <= (others => 'X');
     ept.s   <= 'X';
     ept.v   <= '0';
   end procedure sendCmd;

   constant GEN_DESTUFF_C : boolean := true;

   signal dbg : std_logic_vector(7 downto 0) := (others => 'X');

begin

   P_CLK : process is
   begin
      if ( run ) then
         wait for 0.5 us;
         clk <= not clk;
      else
         wait;
      end if;
   end process P_CLK;

   P_TST : process is 
      variable v   : std_logic_vector(7 downto 0);
      variable l   : natural;
      variable lst : std_logic;
      variable dly : natural;
   begin
      sendCmd(xmit, x"AA", lst => '1');
      sendCmd(xmit, x"AA", lst => '1');
      sendCmd(xmit, x"AA", lst => '1');
      sendCmd(xmit, x"5A");
      sendCmd(xmit, x"5B");
      sendCmd(xmit, COMMA_C, lst => '1');
      wait until rising_edge(clk);
      sendCmd(xmit, x"00");

      sendCmd(xmit, COMMA_C, lst => '1');
      sendCmd(xmit, ESCAP_C);
      sendCmd(xmit, x"01", lst => '1');
      sendCmd(xmit, COMMA_C);
      sendCmd(xmit, x"02", lst => '1');
      sendCmd(xmit, ESCAP_C, lst => '1');
      sendCmd(xmit, ESCAP_C);
      sendCmd(xmit, COMMA_C);
      sendCmd(xmit, ESCAP_C, lst => '1');
      sendCmd(xmit, x"FF",   lst => '1');

      l := 0;
      for i in testVec'range loop
         v := std_logic_vector(to_unsigned(testVec(i), v'length));
         dbg <= v;
         if ( l = 0 ) then
            lst := '1';
            l   := to_integer(unsigned(v(7 downto 5)));
report "frame " & integer'image(l);
         else
            lst := '0';
            l   := l - 1;
         end if;
         dly := to_integer(unsigned(v(4 downto 3)));
         v(7 downto 2) := (others => '0');
report "sending " & integer'image(testVec(i)) & " l " & integer'image(l) & " lst " & std_logic'image(lst) & " dly " & integer'image(dly);
         sendCmd(xmit, v, lst, dly);
      end loop;

      -- make sure last frame ends
      sendCmd(xmit, x"44", lst => '1');

      wait until rising_edge( clk ); -- let octet counter stabilize

      report integer'image(xmit.nTxOcts) & " octets sent #######################";

      W_DONE : for i in 1 to 1000 loop
         wait until rising_edge( clk );
      end loop W_DONE;

      -- never get here if RX process stops the test

      report "Test FAILED -- won't finish" severity failure;

      wait;
   end process P_TST;

   P_REP : process ( clk ) is
   begin
      if ( rising_edge( clk ) ) then
         if ( ( vldOut and rdyOut) = '1' ) then
            report "Read: " & to_hstring( datOut );
         end if;
      end if;
   end process P_REP;

   U_DUT : entity work.ByteStuffer
      generic map (
         COMMA_G => COMMA_C,
         ESCAP_G => ESCAP_C
      )
      port map (
         clk    => clk,
         rst    => rst,

         datInp => xmit.d,
         vldInp => xmit.v,
         rdyInp => rdyInp,
         lstInp => xmit.s,
         datOut => datOut,
         vldOut => vldOut,
         rdyOut => rdyOut
      );

G_NO_DESTUFF: if ( not GEN_DESTUFF_C ) generate
   rdyOut <= rdyOutR;
end generate G_NO_DESTUFF;

G_DESTUFF: if ( GEN_DESTUFF_C ) generate
      signal dly       : natural := 0;
      signal tst       : integer := -1;
      signal flen      : natural := 0;
   begin
   P_REPX : process ( clk ) is
   begin
      if ( rising_edge( clk ) ) then
         if ( nRxOcts = EXPECTED_OCTS_C ) then
            if ( nRxOcts /= xmit.nTxOcts ) then
               report "Test FAILED: Mismatching TX (" & integer'image(xmit.nTxOcts) & ") and RX octets" severity failure;
            end if;
            if ( nRxFram /= xmit.nTxFram ) then
               report "Test FAILED: Mismatching TX and RX frames" severity failure;
            end if;
            report "TEST PASSED";
            run <= false;
         end if;
         if ( ( vldOutX and rdyOutX) = '1' ) then
            nRxOcts <= nRxOcts + 1;
            if ( lstOutX = '1' ) then
               if ( datOutX = x"FF" ) then
                  tst  <= 0;
                  flen <= 0;
               end if;
               nRxFram <= nRxFram + 1;
               report "Read -- destuffed: " & to_hstring( datOutX ) & " L (" & integer'image(nRxFram + 1 ) & ")";
            else
               report "Read -- destuffed: " & to_hstring( datOutX );
            end if;

            if ( tst >= 0 and tst < testVec'length ) then
               if ( lstOutX = '1' ) then
                  assert flen = 0
                     report "Frame length mismatch"
                     severity failure;
                  flen <= testVec(tst)/32;
               else
                  flen <= flen - 1;
               end if;
               assert( unsigned(datOutX(1 downto 0)) = (testVec(tst) mod 4) )
                  report "Test Data Mismatch; expected [" & integer'image(tst) & "] " & integer'image(testVec(tst)) & " got " & integer'image(to_integer(unsigned(datOutX)))
                  severity failure;
               tst <= tst + 1;
            end if;
            dly     <= to_integer(unsigned(datOutX(1 downto 0)));
         end if;
         if ( dly /= 0 ) then
            dly <= dly - 1;
         end if;
      end if;
   end process P_REPX;

   P_RDYOUT : process ( dly ) is
   begin
      if ( dly = 0 ) then
         rdyOutX <= '1';
      else
         rdyOutX <= '0';
      end if;
   end process P_RDYOUT;

   U_DUT1 : entity work.ByteDeStuffer
      generic map (
         COMMA_G => COMMA_C,
         ESCAP_G => ESCAP_C
      )
      port map (
         clk    => clk,
         rst    => rst,

         datInp => datOut,
         vldInp => vldOut,
         rdyInp => rdyOut,
         lstOut => lstOutX,
         datOut => datOutX,
         vldOut => vldOutX,
         rdyOut => rdyOutX,
         synOut => rxSyncd,
         rstOut => rstTst
      );

   P_CHECK_RST : process ( clk ) is
   begin
      if ( rising_edge( clk ) ) then
         assert (rstTst = '0') report "Test FAILED -- spurious reset" severity error;
      end if;
   end process P_CHECK_RST;

end generate G_DESTUFF;


end architecture rtl;
