library ieee;
use     ieee.std_logic_1164.all;
use     ieee.numeric_std.all;
use     ieee.math_real.all;

entity PipelinedRShifterTb is
end entity PipelinedRShifterTb;

architecture sim of PipelinedRShifterTb is
   constant SIGN_EXT_C  : boolean := true;

   signal clk           : std_logic := '0';
   signal rst           : std_logic := '0';
   signal shf           : std_logic_vector(2 downto 0) := "000";
   signal datI          : std_logic_vector(4 downto 0) := "00000";
   signal auxI          : std_logic_vector(7 downto 0) := x"00";
   signal datO          : std_logic_vector(4 downto 0) := "00000";
   signal auxO          : std_logic_vector(7 downto 0) := x"00";
   signal auxOS         : std_logic_vector(7 downto 0) := x"00";

   signal datOS         : std_logic_vector(6 downto 0) := (others => '0');
   signal datIS         : std_logic_vector(6 downto 0) := (others => '0');

   signal run           : boolean := true;

   function cmp(constant a,b,s : in std_logic_vector) return boolean is
      constant RS : integer := to_integer(unsigned(s));
   begin
      if ( SIGN_EXT_C ) then
         return a = std_logic_vector( shift_right(   signed( b ), RS ) );
      else
         return a = std_logic_vector( shift_right( unsigned( b ), RS ) );
      end if;
   end function cmp;
begin

   P_CLK : process is
   begin
      if ( run ) then
         wait for 5 us;
         clk <= not clk;
      else
         wait;
      end if;
   end process P_CLK;

   P_DRV : process is
   begin

      wait until rising_edge(clk);
      datIS <= "0001000";
      datI <= "00001"; shf <= "000"; auxI <= x"01"; wait until rising_edge(clk);
      datIS <= "1001000";
      datI <= "00011"; shf <= "001"; auxI <= x"02"; wait until rising_edge(clk);
      datIS <= "1111111";
      datI <= "00111"; shf <= "010"; auxI <= x"03"; wait until rising_edge(clk);
      datI <= "01111"; shf <= "011"; auxI <= x"04"; wait until rising_edge(clk);
      datI <= "11111"; shf <= "100"; auxI <= x"05"; wait until rising_edge(clk);
      datI <= "11111"; shf <= "101"; auxI <= x"06"; wait until rising_edge(clk);
      datI <= "11100"; shf <= "100"; auxI <= x"07"; wait until rising_edge(clk);
      datI <= "11100"; shf <= "011"; auxI <= x"08"; wait until rising_edge(clk);
      datI <= "11100"; shf <= "010"; auxI <= x"09"; wait until rising_edge(clk);
      datI <= "11100"; shf <= "001"; auxI <= x"0a"; wait until rising_edge(clk);
      datI <= "11100"; shf <= "000"; auxI <= x"0b"; wait until rising_edge(clk);
      wait;
   end process P_DRV;

   P_CHK : process ( clk ) is
      variable i : integer;
      variable k : integer;
      variable e : std_logic_vector(datO'range);
      variable es: std_logic_vector(datOS'range);
   begin
      if ( rising_edge( clk ) ) then
         i := to_integer(unsigned(auxO));
         k := to_integer(unsigned(auxOS));
         case i is
            when 1 | 2 | 3 | 4 | 5 | 7 =>
               e := "00001";
            when 6 | 0 =>
               e := "00000";
            when 8 => 
               e := "00011";
            when 9 => 
               e := "00111";
            when 10 => 
               e := "01110";
            when 11 => 
               e := "11100";
               run <= false;
            when others =>
               assert false report "unexpected stage reached " & integer'image(i);
         end case;

         case k is
            when 1 =>
               es := "0001000";
            when 2 =>
               if ( SIGN_EXT_C ) then es := "1111001"; else es := "0001001"; end if;
            when 3 => 
               if ( SIGN_EXT_C ) then es := "1111111"; else es := "0000001"; end if;
            when others =>
         end case;

         if ( SIGN_EXT_C and ( i >= 5 ) ) then
            L_EXT : for j in e'left downto e'right loop
               if ( e(j) = '1' ) then
                  exit L_EXT;
               else
                  e(j) := '1';
               end if;
            end loop L_EXT;
         end if;

         assert datO = e report "output mismatch @" & integer'image(i) severity failure;

         if ( k <= 3 and k > 0 ) then
            assert datOS = es report "stride-output mismatch @" & integer'image(k) severity failure;
         end if;

         if ( i = 11 ) then
            report "TEST PASSED";
         end if;
      end if;
   end process P_CHK;

   U_DUT : entity work.PipelinedRShifter
      generic map (
         DATW_G          => datI'length,
         AUXW_G          => auxI'length,
         SIGN_EXTEND_G   => SIGN_EXT_C,
         PIPL_SHIFT_G    => true
      )
      port map (
         clk             => clk,
         rst             => rst,

         shift           => shf,

         datInp          => datI,
         auxInp          => auxI,
         datOut          => datO,
         auxOut          => auxO
      );

   U_DUT_S : entity work.PipelinedRShifter
      generic map (
         DATW_G          => datIS'length,
         AUXW_G          => auxI'length,
         SIGN_EXTEND_G   => SIGN_EXT_C,
         PIPL_SHIFT_G    => true,
         STRIDE_G        => 3
      )
      port map (
         clk             => clk,
         rst             => rst,

         shift           => shf,

         datInp          => datIS,
         auxInp          => auxI,
         datOut          => datOS,
         auxOut          => auxOS
      );
       
end architecture sim;
