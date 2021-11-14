library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;
use ieee.math_real.all;

entity WordSpiIF is
   generic (
      CLOCK_FREQ_G : real;
      SPI_FREQ_G   : real      := 5.0E6;
      WIDTH_G      : natural   := 8;
      SCLK_ACT_G   : std_logic := '1'
   );
   port (
      clk          : in  std_logic;
      rst          : in  std_logic;

      rdat         : in  std_logic_vector(WIDTH_G - 1 downto 0);
      rvld         : in  std_logic := '0';
      rrdy         : out std_logic := '0';

      wdat         : out std_logic_vector(WIDTH_G - 1 downto 0);
      wvld         : out std_logic := '0';
      wrdy         : in  std_logic;

      sclk         : out std_logic;
      mosi         : out std_logic;
      miso         : in  std_logic
   );
end entity WordSpiIF;

architecture rtl of WordSpiIF is

   constant DIV_C  : natural := natural( realmax( ceil( CLOCK_FREQ_G / SPI_FREQ_G / 2.0 ), 1.0 ) );

   type StateType is (IDLE, REDGE, FEDGE, DONE);

   type RegType is record
      state        : stateType;
      div          : natural range 0 to DIV_C - 1;
      sclk         : std_logic;
      count        : natural range 0 to WIDTH_G - 1;
      sreg         : std_logic_vector(WIDTH_G downto 0);
      wvld         : std_logic;
   end record RegType;

--   __/--\__/--\__/--\__/--\
--
--  D1 D0

   constant REG_INIT_C : RegType := (
      state        => IDLE,
      div          => 0,
      sclk         => not SCLK_ACT_G,
      count        => 0,
      sreg         => (others => '0'),
      wvld         => '0'
   );

   signal r        : RegType := REG_INIT_C;
   signal rin      : RegType;

begin

   P_COMB : process ( r, rdat, rvld, wrdy, miso ) is
      variable v       : RegType;
      variable rrdyLoc : std_logic;
   begin
      v := r;

      rrdyLoc := '0';

      case ( r.state ) is

         when IDLE  =>
            rrdyLoc := '1';
            if ( (rvld and rrdyLoc) = '1' ) then
               v.sreg(v.sreg'left downto 1) := rdat;
               v.state                      := REDGE; 
               v.div                        := DIV_C   - 1;
               v.count                      := WIDTH_G - 1;
            end if;

         when REDGE =>
            if ( 0 = r.div ) then
               v.sreg(0) := miso;
               v.div     := DIV_C - 1;
               v.state   := FEDGE;
               v.sclk    := '1';
            else
               v.div     := r.div - 1;
            end if;

         when FEDGE =>
            if ( 0 = r.div ) then
               v.sreg     := r.sreg(r.sreg'left - 1 downto 0) & '0';
               v.div      := DIV_C - 1;
               v.sclk     := '0';
               if ( 0 = r.count ) then
                  v.state := DONE;
                  v.wvld  := '1';
               else
                  v.count := r.count - 1;
                  v.state := REDGE;
               end if;
            else
               v.div      := r.div - 1;
            end if;

         when DONE  =>
            if ( (r.wvld and wrdy) = '1' ) then
               v.wvld  := '0';
               v.state := IDLE;
            end if;

      end case; 

      rrdy <= rrdyLoc;
      rin  <= v;
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

   wvld <= r.wvld;
   wdat <= r.sreg(r.sreg'left downto 1);
   mosi <= r.sreg(r.sreg'left);
   sclk <= r.sclk;

end architecture rtl;
