library ieee;

use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity ByteStufferTb is
end entity ByteStufferTb;

architecture rtl of ByteStufferTb is
   signal clk : std_logic := '0';
   signal rst : std_logic := '0';
   signal run : boolean   := true;

   type   EPType is record
      d : std_logic_vector(7 downto 0);
      v : std_logic;
      s : std_logic;
   end record EPType;

   constant EP_INIT_C : EPType := (
      d => (others => 'X'),
      v => '0',
      s => '0'
   );

   signal xmit   : EPType := EP_INIT_C;

   signal rdyInp : std_logic;
   signal datOut : std_logic_vector(7 downto 0);
   signal vldOut : std_logic;
   signal rdyOut : std_logic                    := '1';

   signal datOutX: std_logic_vector(7 downto 0);
   signal vldOutX: std_logic;
   signal sofOutX: std_logic;
   signal rdyOutX: std_logic                    := '1';

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
      constant  sof : std_logic := '0'
   ) is
   begin
     ept.v <= '1';
     ept.d <= dat;
     ept.s <= sof;
     wait until rising_edge( clk );
     while ( (ept.v and rdyInp) = '0' ) loop
        wait until rising_edge( clk );
     end loop;
     ept.d <= (others => 'X');
     ept.s <= 'X';
     ept.v <= '0';
   end procedure sendCmd;

   constant GEN_DESTUFF_C : boolean := true;

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
      variable v : std_logic_vector(7 downto 0);
   begin

      sendCmd(xmit, x"5A", sof => '1');
      sendCmd(xmit, x"5B");
      sendCmd(xmit, x"CA");
      wait until rising_edge(clk);
      sendCmd(xmit, x"00");

      sendCmd(xmit, x"CA", sof => '1');
      sendCmd(xmit, x"55", sof => '1');
      sendCmd(xmit, x"01");
      sendCmd(xmit, x"CA", sof => '1');
      sendCmd(xmit, x"02");

      run <= false;
      wait;
   end process P_TST;

   P_REP : process ( clk ) is
   begin
      if ( rising_edge( clk ) ) then
         if ( ( vldOut and rdyOut) = '1' ) then
            report "Read: " & to_hstring( datOut );
            if ( not GEN_DESTUFF_C ) then
               rdyOut <= not rdyOut;
            end if;
         elsif ( rdyOut = '0' ) then
            if ( not GEN_DESTUFF_C ) then
               rdyOut <= '1';
            end if;
         end if;
      end if;
   end process P_REP;

   U_DUT : entity work.ByteStuffer
      port map (
         clk    => clk,
         rst    => rst,

         datInp => xmit.d,
         vldInp => xmit.v,
         rdyInp => rdyInp,
         sofInp => xmit.s,
         datOut => datOut,
         vldOut => vldOut,
         rdyOut => rdyOut
      );

G_X: if ( true ) generate
   P_REPX : process ( clk ) is
      variable c : character;
   begin
      if ( rising_edge( clk ) ) then
         if ( ( vldOutX and rdyOutX) = '1' ) then
            if ( sofOutX = '1' ) then
               c := 'S';
            else
               c := ' ';
            end if;
            report "Read -- destuffed: " & to_hstring( datOutX ) & " " & c;
 --           rdyOutX <= not rdyOutX;
         elsif ( rdyOutX = '0' ) then
            rdyOutX <= '1';
         end if;
      end if;
   end process P_REPX;

   U_DUT1 : entity work.ByteDeStuffer
      port map (
         clk    => clk,
         rst    => rst,

         datInp => datOut,
         vldInp => vldOut,
         rdyInp => rdyOut,
         sofOut => sofOutX,
         datOut => datOutX,
         vldOut => vldOutX,
         rdyOut => rdyOutX
      );
end generate G_X;


end architecture rtl;
