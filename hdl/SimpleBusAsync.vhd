library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;
use ieee.math_real.all;

use work.BasicPkg.all;
use work.CommandMuxPkg.all;

entity SimpleBusAsync is
   port (
      clkIb     : in  std_logic;
      busIb     : in  SimpleBusMstType := SIMPLE_BUS_MST_INIT_C;
      rdyIb     : out std_logic;

      clkOb     : in  std_logic;
      busOb     : out SimpleBusMstType := SIMPLE_BUS_MST_INIT_C;
      rdyOb     : in  std_logic
   );
end entity SimpleBusAsync;

architecture rtl of SimpleBusAsync is
   attribute KEEP   : string;

   type RegIbType is record
      tgl       : std_logic;
   end record RegIbType;

   constant REG_IB_INIT_C : RegIbType := (
      tgl       => '0'
   );

   type RegObType is record
      tgl       : std_logic;
   end record RegObType;

   constant REG_OB_INIT_C : RegObType := (
      tgl       => '0'
   );

   signal rIb      : RegIbType := REG_IB_INIT_C;
   signal rOb      : RegObType := REG_OB_INIT_C;
   signal rinIb    : RegIbType;
   signal rinOb    : RegObType;

   signal tglM2S   : std_logic;
   signal tglS2M   : std_logic;

   signal busCC    : SimpleBusMstType := SIMPLE_BUS_MST_INIT_C;
   attribute KEEP  of busCC : signal is "TRUE";

   signal rdyIbLoc : std_logic;
   signal vldObLoc : std_logic;

begin

   P_IB_COMB : process ( rIb, busIb, tglS2M ) is
      variable v : RegIbType;
   begin
      v          := rIb;
      rdyIbLoc   <= '0';
      if ( tglS2M = rIb.tgl ) then
         -- idle
         if ( busIb.vld = '1' ) then
            -- consume and latch transaction
            rdyIbLoc <= '1';
            -- post transaction
            v.tgl    := not rIb.tgl;
         end if;
      else
         -- transaction pending
      end if;
      rinIb      <= v;
   end process P_IB_COMB;

   P_IB_SEQ  : process ( clkIb ) is
   begin
      if ( rising_edge( clkIb ) ) then
         rIb <= rinIb;
         if ( ( rdyIbLoc and busIb.vld ) = '1' ) then
            busCC <= busIb;
         end if;
      end if;
   end process P_IB_SEQ;

   P_OB_COMB : process ( rOb, rdyOb, tglM2S, vldObLoc ) is
      variable v   : RegObType;
   begin
      v          := rOb;
      vldObLoc   <= ( tglM2S xor rOb.tgl );
      if ( ( vldObLoc and rdyOb ) = '1' ) then
         v.tgl   := tglM2S;
      end if;
      rinOb      <= v;
   end process P_OB_COMB;

   P_OB_SEQ  : process ( clkOb ) is
   begin
      if ( rising_edge( clkOb ) ) then
         rOb <= rinOb;
      end if;
   end process P_OB_SEQ;

   U_M2S_SYNC : entity work.SynchronizerBit
      port map (
         clk       => clkOb,
         rst       => '0',
         datInp(0) => rIb.tgl,
         datOut(0) => tglM2S
      );

   U_S2M_SYNC : entity work.SynchronizerBit
      port map (
         clk       => clkIb,
         rst       => '0',
         datInp(0) => rOb.tgl,
         datOut(0) => tglS2M
      );

   P_CC_COMB : process( busCC, vldObLoc, rdyIbLoc ) is
   begin 
      busOb     <= busCC;
      busOb.vld <= vldObLoc;
      rdyIb     <= rdyIbLoc;
   end process P_CC_COMB;

end architecture rtl;
