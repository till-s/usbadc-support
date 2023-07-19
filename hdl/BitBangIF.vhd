library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;
use ieee.math_real.all;

use work.BasicPkg.all;

entity BitBangIF is
   generic (
      I2C_SCL_G    : integer := -1; -- index of I2C SCL (to handle clock stretching)
      BBO_INIT_G   : std_logic_vector(7 downto 0) := x"FF";
      I2C_FREQ_G   : real    := 100.0E3;
      CLOCK_FREQ_G : real;
      HPER_WIDTH_G : natural := 1
   );
   port (
      clk          : in  std_logic;
      rst          : in  std_logic;
      
      i2cDis       : in  std_logic := '1'; -- registered together with bbo
      echo         : in  std_logic := '0';

      rdat         : in  std_logic_vector(7 downto 0);
      rvld         : in  std_logic := '0';
      rrdy         : out std_logic := '0';

      wdat         : out std_logic_vector(7 downto 0);
      wvld         : out std_logic := '0';
      wrdy         : in  std_logic;

      bbo          : out std_logic_vector(7 downto 0);
      bbi          : in  std_logic_vector(7 downto 0);

      hper         : in  unsigned(HPER_WIDTH_G - 1 downto 0) := to_unsigned(2-1, HPER_WIDTH_G)
   );
end entity BitBangIF;

architecture rtl of BitBangIF is

   -- need some clock cycles (well, the internal SpiReg does)
   constant HPER_MIN_C    : integer := 2 - 1;

   function I2C_HPER_F return integer is
      variable v : integer;
   begin
      v := integer( ceil ( 0.5*CLOCK_FREQ_G/I2C_FREQ_G ) ) - 1;
      if ( v < 0 ) then
        v := 0;
      end if;
      return v;
   end function I2C_HPER_F;

   function I2C_TIMO_F return integer is
      variable v : integer;
   begin
      v := integer( ceil ( 0.2*CLOCK_FREQ_G ) ) - 1;
      if ( v < I2C_HPER_F ) then
         v := I2C_HPER_F;
      end if;
      return v;
   end function I2C_TIMO_F;

   function max(a,b: integer) return integer is
   begin
     return ite( a > b, a, b );
   end function max;

   constant I2C_LD_TIMO_C : integer := max( numBits( I2C_TIMO_F ), HPER_WIDTH_G );

   type RegType is record
      i2c_timo      : unsigned(I2C_LD_TIMO_C - 1 downto 0);
      bbo           : std_logic_vector(7 downto 0);
      wvld          : std_logic;
      wdat          : std_logic_vector(7 downto 0);
   end record RegType;

   constant REG_INIT_C : RegType := (
      i2c_timo      => (others => '0'),
      bbo           => BBO_INIT_G,
      wvld          => '0',
      wdat          => (others => '0')
   );

   signal r               : RegType := REG_INIT_C;
   signal rin             : RegType;

   signal bbiSync         : std_logic_vector(bbi'range);

   signal sclInEqualsOut  : boolean;

begin

   assert false report "HPER " & integer'image(I2C_HPER_F) severity warning;
   assert false report "TIMO " & integer'image(I2C_TIMO_F) severity warning;
   assert false report "LDTM " & integer'image(I2C_LD_TIMO_C) severity warning;

   GEN_SYNC : if ( I2C_SCL_G >= 0 and I2C_SCL_G < 8 ) generate

      signal sclSync : std_logic;

   begin

      U_SYNC : entity work.SynchronizerBit
         generic map (
            RSTPOL_G    => BBO_INIT_G(I2C_SCL_G)
         )
         port map (
            clk         => clk,
            rst         => rst,
            datInp(0)   => bbi(I2C_SCL_G),
            datOut(0)   => sclSync
         );

      P_SYNC : process (bbi, sclSync) is
         variable v : std_logic_vector(bbi'range);
      begin
         v            := bbi;
         v(I2C_SCL_G) := sclSync;
         bbiSync      <= v;
      end process P_SYNC;

      -- condition to check for clock stretching
      -- readback of SCL must match what we set
      -- on the i2c bus.
      sclInEqualsOut <= (sclSync = r.bbo(I2C_SCL_G)) or ((echo or i2cDis) = '1') ;
         
   end generate GEN_SYNC;

   GEN_NO_SYNC : if ( I2C_SCL_G < 0 or I2C_SCL_G >= 8 ) generate

      bbiSync        <= bbi;
      sclInEqualsOut <= true;

   end generate GEN_NO_SYNC;

   P_COMB : process ( r, rdat, rvld, wrdy, bbiSync, sclInEqualsOut, i2cDis, echo, hper ) is
      variable v       : RegType;
      variable rrdyLoc : std_logic;

   begin
      v := r;

      -- check if our output data was fetched
      if ( wrdy = '1' and r.wvld = '1' ) then
         v.wvld := '0';
      end if;

      -- handle timeout
      v.i2c_timo := r.i2c_timo - 1;

      -- block reception of new data 
      rrdyLoc := '0';
      -- if i2c_timeo is already 0 then this logic reverts the decrement
      if ( r.i2c_timo < (I2C_TIMO_F - I2C_HPER_F) ) then
         if ( sclInEqualsOut or (r.i2c_timo = 0) ) then
            v.i2c_timo := (others => '0');
            -- OK to receive new data (if register is currently emptied or already empty)
            rrdyLoc := not v.wvld;
         end if;
      end if;

      -- receive data
      if ( (rrdyLoc = '1') and (rvld = '1') ) then
         if ( echo = '0' ) then
            v.bbo  := rdat;
            -- record inputs
            v.wdat := bbiSync;
         else
            v.wdat := rdat;
         end if;
         v.wvld := '1';
         if ( (echo or i2cDis) = '0' ) then
            v.i2c_timo := to_unsigned(I2C_TIMO_F, v.i2c_timo'length);
         else
            v.i2c_timo := to_unsigned(I2C_TIMO_F - I2C_HPER_F - 1, v.i2c_timo'length);
            v.i2c_timo := v.i2c_timo + resize( hper, v.i2c_timo'length );
         end if;
      end if;

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
   wdat <= r.wdat;
   bbo  <= r.bbo;

end architecture rtl;
