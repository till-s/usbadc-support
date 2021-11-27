library ieee;

use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity CicFilterTb is
end entity CicFilterTb;

architecture sim of CicFilterTb is

   constant D_W_C   : natural   := 5;
   constant L_D_C   : natural   := 3;
   -- currently supported combinations: DECM: 1, N : any
   --                                   DECM: 5, N : 2
   constant N_S_C   : natural   := 2; --3;
   constant DECM_C  : positive  := 5; --1;

   constant O_W_C   : natural   := D_W_C + N_S_C * L_D_C;
   

   signal clk       : std_logic := '0';
   signal rst       : std_logic := '0';
   signal run       : boolean   := true;
   signal strb      : std_logic;

   signal dataInp   : signed(D_W_C - 1 downto 0)   := (others => '0');
   signal decm      : unsigned(L_D_C - 1 downto 0) := to_unsigned(DECM_C-1, L_D_C);
   signal dataOut   : signed(O_W_C - 1 downto 0);
   signal dovrInp   : std_logic                    := '0';
   signal dovrOut   : std_logic;

   signal count     : natural                      := 0;

   type   DataArray is array (natural range <>) of signed(D_W_C - 1 downto 0);
   type   IntArray  is array (natural range <>) of integer;

   constant DATA_C  : DataArray := (
      to_signed( 9, D_W_C),
      to_signed( 5, D_W_C),
      to_signed( -14, D_W_C),
      to_signed( -8, D_W_C),
      to_signed( 14, D_W_C),
      to_signed( -9, D_W_C),
      to_signed( 3, D_W_C),
      to_signed( 10, D_W_C),
      to_signed( 8, D_W_C),
      to_signed( -2, D_W_C),
      to_signed( 7, D_W_C),
      to_signed( 6, D_W_C),
      to_signed( 0, D_W_C),
      to_signed( 14, D_W_C),
      to_signed( -11, D_W_C),
      to_signed( -13, D_W_C),
      to_signed( 5, D_W_C),
      to_signed( 4, D_W_C),
      to_signed( -9, D_W_C),
      to_signed( 3, D_W_C),
      to_signed( -9, D_W_C),
      to_signed( -7, D_W_C),
      to_signed( -12, D_W_C),
      to_signed( -14, D_W_C),
      to_signed( -14, D_W_C),
      to_signed( 7, D_W_C),
      to_signed( 3, D_W_C),
      to_signed( 5, D_W_C),
      to_signed( -7, D_W_C),
      to_signed( -3, D_W_C),
      to_signed( -1, D_W_C),
      to_signed( 4, D_W_C),
      to_signed( 2, D_W_C),
      to_signed( 3, D_W_C),
      to_signed( -14, D_W_C),
      to_signed( -9, D_W_C),
      to_signed( 9, D_W_C),
      to_signed( -7, D_W_C),
      to_signed( -1, D_W_C),
      to_signed( 11, D_W_C),
      to_signed( -3, D_W_C),
      to_signed( 8, D_W_C),
      to_signed( 3, D_W_C),
      to_signed( 9, D_W_C),
      to_signed( 7, D_W_C),
      to_signed( 1, D_W_C),
      to_signed( 15, D_W_C),
      to_signed( 15, D_W_C),
      to_signed( -4, D_W_C),
      to_signed( -6, D_W_C),
      to_signed( 14, D_W_C),
      to_signed( 6, D_W_C),
      to_signed( -12, D_W_C),
      to_signed( 0, D_W_C),
      to_signed( -5, D_W_C),
      to_signed( 0, D_W_C),
      to_signed( -5, D_W_C),
      to_signed( 4, D_W_C),
      to_signed( -9, D_W_C),
      to_signed( -1, D_W_C),
      to_signed( 2, D_W_C),
      to_signed( 5, D_W_C),
      to_signed( -13, D_W_C),
      to_signed( 14, D_W_C),
      to_signed( -11, D_W_C),
      to_signed( -9, D_W_C),
      to_signed( -9, D_W_C),
      to_signed( 12, D_W_C),
      to_signed( -1, D_W_C),
      to_signed( 4, D_W_C),
      to_signed( 6, D_W_C),
      to_signed( 6, D_W_C),
      to_signed( -2, D_W_C),
      to_signed( -13, D_W_C),
      to_signed( -13, D_W_C),
      to_signed( 1, D_W_C),
      to_signed( -14, D_W_C),
      to_signed( -5, D_W_C),
      to_signed( -3, D_W_C),
      to_signed( -1, D_W_C),
      to_signed( 4, D_W_C),
      to_signed( -6, D_W_C),
      to_signed( -5, D_W_C),
      to_signed( 0, D_W_C),
      to_signed( -5, D_W_C),
      to_signed( 1, D_W_C),
      to_signed( -12, D_W_C),
      to_signed( 8, D_W_C),
      to_signed( 0, D_W_C),
      to_signed( -6, D_W_C),
      to_signed( -8, D_W_C),
      to_signed( -5, D_W_C),
      to_signed( -8, D_W_C),
      to_signed( -9, D_W_C),
      to_signed( -3, D_W_C),
      to_signed( -6, D_W_C),
      to_signed( 5, D_W_C),
      to_signed( 15, D_W_C),
      to_signed( 1, D_W_C),
      to_signed( -15, D_W_C)
   );

   -- results obtained from scilab simulation
   constant RESULT_EXPECTED_N2_DECM5_C : IntArray(20 downto 0) := (
      0,
      21,
      20,
      115,
      -44,
      -153,
      -84,
      -11,
      -60,
      87,
      165,
      62,
      -65,
      -20,
      -66,
      37,
      -162,
      -61,
      -61,
      -125,
      -38
   );

   type ExpectedType is record
      cnt            : integer;
      len            : integer;
      data           : integer;
   end record ExpectedType;

   function EXPECTED_INIT_F return ExpectedType is
      variable v : ExpectedType;
   begin

      v.data := 0;
      if ( DECM_C = 1 ) then
         v.len := DATA_C'length;
         v.cnt := DATA_C'length - 1 + N_S_C - 1;
      elsif ( DECM_C = 5 and N_S_C = 2 ) then
         v.len := RESULT_EXPECTED_N2_DECM5_C'length;
         v.cnt := RESULT_EXPECTED_N2_DECM5_C'length - 1;
      else
         assert false report "Parameter combination not supported by test" severity failure;
      end if;
      return v;
   end function EXPECTED_INIT_F;
   

   signal expected       : ExpectedType := EXPECTED_INIT_F;

begin

   P_MUX : process (expected) is
   begin
      if ( expected.cnt >= expected.len or expected.cnt < 0 ) then
         expected.data <= 0;
      else
         if ( DECM_C = 1 ) then
            expected.data <= to_integer( DATA_C( DATA_C'length - 1 - expected.cnt ) );
         elsif ( DECM_C = 5 and N_S_C = 2 ) then
            expected.data <= RESULT_EXPECTED_N2_DECM5_C( expected.cnt );
         else
            expected.data <= -1234568;
         end if;
      end if;
   end process P_MUX;

   P_CLK : process is
   begin
      if ( run ) then
         wait for 10 us;
         clk <= not clk;
      else
         wait;
      end if;
   end process P_CLK;

   dataInp <= DATA_C(count);

   P_DRV : process( clk ) is
   begin
      if ( rising_edge( clk ) ) then
         if ( count < DATA_C'length - 1 ) then
            count   <= count + 1;
         end if;
         if ( strb = '1' ) then
            if ( expected.cnt >= 0 ) then
               report integer'image(to_integer(dataOut));
               assert to_integer(dataOut) = expected.data
                  report "RES_N2_DECM5 mismatch @" & integer'image(expected.cnt)
                         & " expected " & integer'image(expected.data)
                  severity failure;
               expected.cnt <= expected.cnt - 1;
            end if;
         end if;
         if ( expected.cnt < 0 ) then
            report "TEST PASSED";
            run <= false;
         end if;
      end if;
   end process P_DRV;

   U_DUT : entity work.CicFilter
      generic map (
         DATA_WIDTH_G => D_W_C,
         LD_MAX_DCM_G => L_D_C,
         NUM_STAGES_G => N_S_C
      )
      port map (
         clk          => clk,
         rst          => rst,

         decmInp      => decm,
         cenbOut      => open,
         cenbInp      => open,

         dataInp      => dataInp,
         dovrInp      => dovrInp,
         dataOut      => dataOut,
         dovrOut      => dovrOut,
         strbOut      => strb
      );

end architecture sim; 
