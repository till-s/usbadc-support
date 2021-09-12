library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;
use ieee.math_real.all;

use work.CommandMuxPkg.all;

entity CommandBitBang is
   generic (
      I2C_SCL_G    : integer := -1; -- index of I2C SCL (to handle clock stretching)
      BBO_INIT_G   : std_logic_vector(7 downto 0) := x"FF";
      I2C_FREQ_G   : real    := 100.0E3;
      CLOCK_FREQ_G : real
   );
   port (
      clk          : in  std_logic;
      rst          : in  std_logic;
      
      mIb          : in  SimpleBusMstType;
      rIb          : out std_logic;

      mOb          : out SimpleBusMstType;
      rOb          : in  std_logic;

      bbo          : out std_logic_vector(7 downto 0);
      bbi          : in  std_logic_vector(7 downto 0)
   );
end entity CommandBitBang;

architecture rtl of CommandBitBang is

   type StateType is (ECHO, FWD);

   type RegType is record
      state         : StateType;
      cmd           : std_logic_vector(7 downto 0);
      lstSeen       : std_logic;
   end record RegType;

   constant REG_INIT_C : RegType := (
      state         => ECHO,
      cmd           => (others => '0'),
      lstSeen       => '0'
   );

   signal r               : RegType := REG_INIT_C;
   signal rin             : RegType;

   signal wdat            : std_logic_vector(7 downto 0);
   signal wvld            : std_logic;
   signal wrdy            : std_logic;

   signal rvld            : std_logic;
   signal rrdy            : std_logic;

   signal i2cDis          : std_logic;

   constant I2C_DIS_BIT_C : natural := 4;

begin

   assert I2C_DIS_BIT_C >= NUM_CMD_BITS_C;

   i2cDis <= r.cmd( I2C_DIS_BIT_C );

   P_COMB : process ( r, mIb, rOb, wdat, wvld, rrdy ) is
      variable v       : RegType;
   begin
      v := r;

      mOb.dat <= mIb.dat;
      mOb.vld <= mIb.vld;
      mOb.lst <= r.lstSeen;

      rIb     <= rOb;
      rvld    <= '0';
      wrdy    <= '1'; -- drop - just in case

      case ( r.state ) is
         when ECHO =>
            v.lstSeen := '0';
            if ( (rOb and mIb.vld) = '1' ) then
               v.cmd := mIb.dat;
               if ( mIb.lst /= '1' ) then
                  v.state := FWD;
               end if;
            end if;
         when FWD  =>
            mOb.dat <= wdat;
            mOb.vld <= wvld;
            wrdy    <= rOb;

            if ( r.lstSeen = '0' ) then
               rvld    <= mIb.vld;
               rIb     <= rrdy;
            else
               rvld    <= '0';
               rIb     <= '0'; -- wait until frame is send
            end if;

            if ( (rrdy and mIb.vld and mIb.lst) = '1' ) then
               v.lstSeen := '1';
            end if;

            if ( ( rOb and wvld and r.lstSeen ) = '1' ) then
               v.state   := ECHO;
               v.lstSeen := '0';
            end if;

      end case;

      rin     <= v;
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

   U_BB : entity work.BitBangIF
      generic map (
         I2C_SCL_G    => I2C_SCL_G,
         BBO_INIT_G   => BBO_INIT_G,
         I2C_FREQ_G   => I2C_FREQ_G,
         CLOCK_FREQ_G => CLOCK_FREQ_G
      )
      port map (
         clk          => clk,
         rst          => rst,
         
         i2cDis       => i2cDis,

         rdat         => mIb.dat,
         rvld         => rvld,
         rrdy         => rrdy,

         wdat         => wdat,
         wvld         => wvld,
         wrdy         => wrdy,

         bbo          => bbo,
         bbi          => bbi
      );

end architecture rtl;
