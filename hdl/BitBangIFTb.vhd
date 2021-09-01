library ieee;

use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity BitBangIFTb is
end entity BitBangIFTb;

architecture rtl of BitBangIFTb is

   constant CLOCK_FREQ_C : real := 24.0E6;

   constant I2C_SCL_C : natural := 7;

   signal clk : std_logic := '0';
   signal rst : std_logic := '0';
   signal run : boolean   := true;

   signal rvld: std_logic := '1';
   signal rrdy: std_logic;
   signal rdat: std_logic_vector(7 downto 0) := (others => '0');

   signal wvld: std_logic;
   signal wrdy: std_logic := '1';
   signal wdat: std_logic_vector(7 downto 0);

   signal bbi : std_logic_vector(7 downto 0) := (others => '0');
   signal bbo : std_logic_vector(7 downto 0) := (others => '0');

   signal i2cDis : std_logic := '1';

begin

   P_CLK : process is
   begin
      if ( run ) then
         wait for (0.5/CLOCK_FREQ_C) * 1 sec;
         clk <= not clk;
      else
         wait;
      end if;
   end process P_CLK;

   P_TST : process is 
      variable i : integer;
   begin
      i := 0;
      while ( i < 20 ) loop
         wait until rising_edge( clk );
         if ( (rvld and rrdy) = '1' ) then
         bbi (I2C_SCL_C - 1 downto 0) <= std_logic_vector( to_unsigned( i, 7 )  );
         rdat(I2C_SCL_C - 1 downto 0) <= std_logic_vector( to_unsigned(50+i, 7) );
         rdat(I2C_SCL_C) <= not rdat(I2C_SCL_C);
         if ( i = 9 ) then
            i2cDis <= '0';
         end if;
         i := i + 1;
         end if;
      end loop;

      run <= false;
      wait;
   end process P_TST;

   P_STALL : process ( clk ) is 
   begin
      if ( rising_edge( clk ) ) then
         if ( to_integer(unsigned(bbi(I2C_SCL_C - 1 downto 0))) > 4 ) then
            if ( (wvld and wrdy) = '1' ) then
               wrdy <= '0';
            elsif (wrdy = '0') then
               wrdy <= '1';
            end if;
         end if;
      end if;
   end process P_STALL;


   P_REP1 : process (bbo) is
   begin
      report "New bbo " & integer'image(to_integer(unsigned(bbo(I2C_SCL_C - 1 downto 0))));
   end process P_REP1;


   P_REP2 : process (wdat, wvld, wrdy) is
   begin
      if ( (wvld = '1') and (wrdy = '1') ) then
         report "New wdat " & integer'image(to_integer(unsigned(wdat(I2C_SCL_C - 1 downto 0))));
      end if;
   end process P_REP2;

   U_DUT : entity work.BitBangIF
   generic map (
      I2C_SCL_G    => I2C_SCL_C,
      CLOCK_FREQ_G => CLOCK_FREQ_C
   )
   port map (
      clk          => clk, 
      rst          => rst, 
      
      i2cDis       => i2cDis,

      rdat         => rdat,
      rvld         => rvld,
      rrdy         => rrdy,

      wdat         => wdat,
      wvld         => wvld,
      wrdy         => wrdy,

      bbo          => bbo,
      bbi          => bbi
   );

   bbi(I2C_SCL_C) <= bbo(I2C_SCL_C) after 100 ns;

end architecture rtl;
