library ieee;
use     ieee.std_logic_1164.all;

-- a simple register with spi interface

entity SpiReg is
   generic (
      NUM_BITS_G : natural := 8;
      INIT_VAL_G : std_logic_vector := ""
   );
   port (
      clk        : in  std_logic;
      rst        : in  std_logic := '0';

      -- spi interface
      sclk       : in  std_logic;
      scsb       : in  std_logic;
      mosi       : in  std_logic;
      miso       : out std_logic;

      -- data interface
      data_inp   : in  std_logic_vector(NUM_BITS_G - 1 downto 0);
      rs         : out std_logic;
      
      data_out   : out std_logic_vector(NUM_BITS_G - 1 downto 0);
      ws         : out std_logic
   );
end entity SpiReg;

architecture rtl of SpiReg is

   function INIT_VAL_F return std_logic_vector is
      variable v : std_logic_vector(NUM_BITS_G downto 0);
   begin
      assert INIT_VAL_G'length = 0 or INIT_VAL_G'length = NUM_BITS_G
         report "Invalid length of INIT_VAL_G" severity failure;
      if ( INIT_VAL_G'length = 0 ) then
         v := (others => '0');
      else
         v := INIT_VAL_G(INIT_VAL_G'left) & INIT_VAL_G;
      end if;
      return v;
   end function INIT_VAL_F;

   type RegType is record
      lsclk      : std_logic;
      lscsb      : std_logic;
      sr         : std_logic_vector(NUM_BITS_G downto 0);
      cnt        : natural;
   end record RegType;

   constant REG_INIT_C : RegType := (
      lsclk      => '0',
      lscsb      => '1',
      sr         => INIT_VAL_F,
      cnt        => NUM_BITS_G - 1
   );

   signal r      : RegType := REG_INIT_C;
   signal rin    : RegType;

   signal rsLoc  : std_logic;
   signal wsLoc  : std_logic;

begin

   rsLoc <= not scsb and r.lscsb;
   wsLoc <=     scsb and not r.lscsb;

   P_COMB : process( r, sclk, scsb, mosi, data_inp, rsLoc, wsLoc ) is
      variable v : RegType;
   begin
      v := r;

      v.lscsb := scsb;
      v.lsclk := sclk;

      if     ( rsLoc = '1' ) then
         v.sr(r.sr'left - 1 downto 0) := data_inp;
         v.sr(r.sr'left)              := data_inp(data_inp'left);
      end if;

      if ( scsb = '0' ) then
         if ( (sclk and not r.lsclk) = '1' ) then
            -- rising sclk
            v.sr(v.sr'left - 1 downto 0) := r.sr(r.sr'left - 2 downto 0) & mosi;
         elsif ( (not sclk and r.lsclk) = '1' ) then
            -- falling sclk
            v.sr(v.sr'left) := r.sr(r.sr'left - 1);
         end if;
      else
         v.lsclk := '0';
      end if;

      rin <= v;
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

   miso     <= r.sr(r.sr'left);
   rs       <= rsLoc;
   ws       <= wsLoc;
   data_out <= r.sr(r.sr'left - 1 downto 0);

end architecture rtl;
