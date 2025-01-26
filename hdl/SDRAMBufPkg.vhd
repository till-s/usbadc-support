library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;
use ieee.math_real.all;

package SDRAMBufPkg is

   constant SDRAM_D_WIDTH_C : natural := 16;
   -- not all of these bits are used; depends
   -- on the actual device
   constant SDRAM_A_WIDTH_C : natural := 31;

   type SDRAMReqType is record
      req                : std_logic;
      rdnwr              : std_logic;
      wdat               : std_logic_vector(SDRAM_D_WIDTH_C - 1 downto 0);
      addr               : std_logic_vector(SDRAM_A_WIDTH_C - 1 downto 0);
   end record SDRAMReqType;

   constant SDRAM_REQ_INIT_C : SDRAMReqType := (
      req                => '0',
      rdnwr              => '0',
      wdat               => (others => '0'),
      addr               => (others => '0')
   );
 
   type SDRAMRepType is record
      -- request ack
      ack                : std_logic;
      -- read data valid (accounting for pipeline delay)
      vld                : std_logic;
      rdat               : std_logic_vector(SDRAM_D_WIDTH_C - 1 downto 0);
      -- ready for operation/init done
      rdy                : std_logic;
   end record SDRAMRepType;

   constant SDRAM_REP_INIT_C : SDRAMRepType := (
      ack                => '0',
      vld                => '0',
      rdat               => (others => '0'),
      rdy                => '0'
   );
  
end package SDRAMBufPkg;
