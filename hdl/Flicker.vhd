library ieee;
use     ieee.std_logic_1164.all;
use     ieee.numeric_std.all;
use     ieee.math_real.all;

use     work.BasicPkg.all;

-- make sure an LED marking activity stays off for some minimum time

entity Flicker is
   generic (
      CLOCK_FREQ_G     : real; -- hz
      HOLD_TIME_G      : real; -- s
      ACTIVE_G         : std_logic := '1'
   );
   port (
      clk              : in  std_logic;
      rst              : in  std_logic := '0';
      datInp           : in  std_logic;
      datOut           : out std_logic
   );
end entity Flicker;

architecture rtl of Flicker is
   constant TICKS_C      : integer := integer( round( CLOCK_FREQ_G * HOLD_TIME_G ) ) - 2;
   constant WIDTH_C      : natural := numBits( TICKS_C );

   subtype  TimerType    is signed(WIDTH_C downto 0);

   constant TIMER_INIT_C : TimerType := (others => '1');

   signal   timer        : TimerType := TIMER_INIT_C;
   signal   ldin         : std_logic := not ACTIVE_G;
begin

   P_FLICK : process ( clk ) is
   begin
      if ( rising_edge( clk ) ) then
         if    ( rst = '1' ) then
            timer <= TIMER_INIT_C;
            ldin  <= not ACTIVE_G;
         else
            ldin  <= datInp;
            if    ( timer(timer'left) = '0' ) then
               timer <= timer - 1;
            elsif ( ( datInp = not ACTIVE_G ) and ( ldin = ACTIVE_G ) ) then
               timer <= to_signed( TICKS_C, timer'length );
            end if;
         end if;
      end if;
   end process P_FLICK;

   datOut <= datInp when timer(timer'left) = '1' else not ACTIVE_G;

end architecture rtl;
