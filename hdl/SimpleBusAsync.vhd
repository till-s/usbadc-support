library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;
use ieee.math_real.all;

use work.BasicPkg.all;
use work.CommandMuxPkg.all;

entity SimpleBusAsync is
   port (
      clkIb     : in  std_logic;
      rstIb     : in  std_logic := '0';
      busIb     : in  SimpleBusMstType := SIMPLE_BUS_MST_INIT_C;
      rdyIb     : out std_logic;
      rstBsy    : out std_logic;

      clkOb     : in  std_logic;
      rstOb     : out std_logic;
      busOb     : out SimpleBusMstType := SIMPLE_BUS_MST_INIT_C;
      rdyOb     : in  std_logic
   );
end entity SimpleBusAsync;

architecture rtl of SimpleBusAsync is
   attribute KEEP         : string;
   attribute SYN_KEEP     : boolean;

   type RegIbType is record
      tgl       : std_logic;
      rst       : std_logic;
   end record RegIbType;

   constant REG_IB_INIT_C : RegIbType := (
      tgl       => '0',
      rst       => '0'
   );

   type RegObType is record
      tgl       : std_logic;
      rst       : std_logic;
   end record RegObType;

   constant REG_OB_INIT_C : RegObType := (
      tgl       => '0',
      rst       => '0'
   );

   signal rIb      : RegIbType := REG_IB_INIT_C;
   signal rOb      : RegObType := REG_OB_INIT_C;
   signal rinIb    : RegIbType;
   signal rinOb    : RegObType;

   signal tglM2S   : std_logic;
   signal rstM2S   : std_logic;
   signal tglS2M   : std_logic;
   signal rstS2M   : std_logic;

   signal busCC    : SimpleBusMstType := SIMPLE_BUS_MST_INIT_C;
   attribute KEEP         of busCC : signal is "TRUE";
   attribute SYN_KEEP     of busCC : signal is true;

   signal rdyIbLoc : std_logic;
   signal vldObLoc : std_logic;

begin

   P_IB_COMB : process ( rIb, busIb, tglS2M, rstIb, rstS2M ) is
      variable v : RegIbType;
   begin
      v          := rIb;
      rdyIbLoc   <= '0';
      if ( rstIb = '1' ) then
         v.rst := '1';
      elsif ( (rstS2M or rIb.rst) = '1' ) then
         -- reset ongoing; dont' touch the toggle flag
         -- to avoid glitches (synchronizer introduces
         -- potential glitches unless rst is sent earlier than
         -- tgl but we don't want to delay tgl).
         if ( rstS2M = '1' ) then
            -- received ACK; end the reset phase
            v.rst := '0';
         end if;
      else
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

   P_OB_COMB : process ( rOb, rdyOb, tglM2S, rstM2S, vldObLoc ) is
      variable v   : RegObType;
   begin
      v          := rOb;
      v.rst      := rstM2S;
      if ( rstM2S = '1' ) then
         -- send toggle back earlier than reset so that
         -- the other side receives the aborted toggle
         -- before they see the reset ack.
         v.tgl    := tglM2S;
         -- still allow this cycle to terminate (process continues
         -- normally) if (rldObLoc and rdOb) = '1' happens to hold...
      end if;
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
      generic map (
         WIDTH_G   => 2
      )
      port map (
         clk       => clkOb,
         rst       => '0',
         datInp(0) => rIb.tgl,
         datInp(1) => rIb.rst,
         datOut(0) => tglM2S,
         datOut(1) => rstM2S
      );

   U_S2M_SYNC : entity work.SynchronizerBit
      generic map (
         WIDTH_G   => 2
      )
      port map (
         clk       => clkIb,
         rst       => '0',
         datInp(0) => rOb.tgl,
         datInp(1) => rOb.rst,
         datOut(0) => tglS2M,
         datOut(1) => rstS2M
      );

   P_CC_COMB : process( busCC, vldObLoc, rdyIbLoc ) is
   begin 
      busOb     <= busCC;
      busOb.vld <= vldObLoc;
      rdyIb     <= rdyIbLoc;
   end process P_CC_COMB;

   rstBsy <= rIb.rst or rstS2M;
   rstOb  <= rOb.rst;

end architecture rtl;
