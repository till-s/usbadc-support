library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;
use ieee.math_real.all;

package CommandMuxPkg is

   type Slv8Array is array(integer range <>) of std_logic_vector(7 downto 0);

   type SimpleBusMstType is record
      vld : std_logic;
      lst : std_logic;
      dat : std_logic_vector(7 downto 0);
   end record SimpleBusMstType;

   constant SIMPLE_BUS_MST_INIT_C : SimpleBusMstType := (
      vld => '0',
      lst => 'X',
      dat => (others => 'X')
   );

   type SimpleBusMstArray is array (natural range <>) of SimpleBusMstType;

   constant NUM_CMD_BITS_C : natural := 4;
   -- command 0xF is reserved
   constant NUM_CMD_MAX_C  : natural := 2**NUM_CMD_BITS_C - 1;

   subtype  SubCommandBBType  is std_logic_vector(2 downto 0);

   constant CMD_BB_NONE_C     : SubCommandBBType := SubCommandBBType( to_unsigned( 0, SubCommandBBType'length ) );
   constant CMD_BB_SPI_ROM_C  : SubCommandBBType := SubCommandBBType( to_unsigned( 1, SubCommandBBType'length ) );
   constant CMD_BB_SPI_ADC_C  : SubCommandBBType := SubCommandBBType( to_unsigned( 2, SubCommandBBType'length ) );
   constant CMD_BB_SPI_PGA_C  : SubCommandBBType := SubCommandBBType( to_unsigned( 3, SubCommandBBType'length ) );
   constant CMD_BB_I2C_C      : SubCommandBBType := SubCommandBBType( to_unsigned( 4, SubCommandBBType'length ) );

   function subCommandBBGet(constant cmd : std_logic_vector(7 downto 0)) return SubCommandBBType;

   subtype  SubCommandAcqType is std_logic_vector(1 downto 0);
   constant CMD_ACQ_READ_C    : SubCommandAcqType := SubCommandAcqType( to_unsigned( 0, SubCommandAcqType'length ) );
   constant CMD_ACQ_FLUSH_C   : SubCommandAcqType := SubCommandAcqType( to_unsigned( 1, SubCommandAcqType'length ) );

   function subCommandAcqGet(constant cmd : std_logic_vector(7 downto 0)) return SubCommandAcqType;

   function numBits(constant x : integer) return integer;

end package CommandMuxPkg;

package body CommandMuxPkg is

   function numBits(constant x : integer) return integer is
   begin
      if ( x = 0 ) then return 1; end if;
      return integer( floor( log2( real( x ) ) ) ) + 1;
   end function numBits;

   function subCommandBBGet(constant cmd : std_logic_vector(7 downto 0)) return SubCommandBBType is
   begin
      return SubCommandBBType( cmd(NUM_CMD_BITS_C + SubCommandBBType'length - 1 downto NUM_CMD_BITS_C) );
   end function subCommandBBGet;

   function subCommandAcqGet(constant cmd : std_logic_vector(7 downto 0)) return SubCommandAcqType is
   begin
      return SubCommandAcqType( cmd(NUM_CMD_BITS_C + SubCommandAcqType'length - 1 downto NUM_CMD_BITS_C) );
   end function subCommandAcqGet;

end package body CommandMuxPkg;
