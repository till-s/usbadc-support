library ieee;
use     ieee.std_logic_1164.all;
use     ieee.numeric_std.all;

use     work.BasicPkg.all;

-- shadow registers for SPI write-only devices;
-- provide read-back of current values.
-- NOTES: - Supports SPI mode 0 only!
--        - clk must be at least 4x as fast as sclkIb


entity SpiShadowReg is
   generic (
      NUM_REGS_G         : positive range 1 to 31;
      -- register initialization (optional); *ascending* index
      REG_INIT_G         : Slv8Array := SLV8_ARRAY_EMPTY_C
   );
   port (
      clk                : in  std_logic;
      rst                : in  std_logic := '0';

      sclkIb             : in  std_logic;
      mosiIb             : in  std_logic;
      scsbIb             : in  std_logic;
      misoIb             : out std_logic;

      sclkOb             : out std_logic_vector(NUM_REGS_G - 1 downto 0);
      scsbOb             : out std_logic_vector(NUM_REGS_G - 1 downto 0)
      -- no misoOb since the registers we shadow are supposed to be
      -- write-only
   );
end entity SpiShadowReg;

architecture rtl of SpiShadowReg is

   type StateType is ( IDLE, DATA, DEASS_CS, DRAIN );

   constant OP_READ_C  : std_logic := '1';
   constant CLK_ACT_C  : std_logic := '1';

   function REG_INIT_F return Slv8Array is
      variable v : Slv8Array(0 to NUM_REGS_G - 1);
   begin
      if ( REG_INIT_G'length = 0 ) then
         v := (others => (others => '0'));
      else
         v := REG_INIT_G;
      end if;
      return v;
   end function;

   type RegType is record
      state       : StateType;
      lscsb       : std_logic;
      regs        : Slv8Array(0 to NUM_REGS_G - 1);
      sel         : natural range 0 to NUM_REGS_G - 1;
      op          : std_logic;
      phas        : std_logic;
   end record RegType;

   constant REG_INIT_C : RegType := (
      state       => IDLE,
      lscsb       => '1',
      regs        => REG_INIT_F,
      sel         => 0,
      op          => OP_READ_C,
      phas        => '0'
   );

   signal r                : RegType := REG_INIT_C;
   signal rin              : RegType := REG_INIT_C;

   signal rs               : std_logic;
   signal ws               : std_logic;
   signal shftDatIb        : Slv8Type;
   signal shftDatOb        : Slv8Type := (others => '1');

begin

   U_SR : entity work.SpiReg
      generic map (
         FRAMED_G     => true
      )
      port map (
         clk          => clk,
         rst          => rst,
         sclk         => sclkIb,
         scsb         => scsbIb,
         mosi         => mosiIb,
         miso         => misoIb,
         data_inp     => shftDatOb,
         data_out     => shftDatIb,
         rs           => rs,
         ws           => ws
      );

   P_COMB : process ( r, sclkIb, mosiIb, scsbIb, rs, ws, shftDatIb ) is
      variable v            : RegType;
      variable propagateSPI : boolean;
   begin
      v            := r;

      shftDatOb    <= ( others => '1' );

      v.lscsb      := scsbIb;
      propagateSPI := false;
      sclkOb       <= ( others => not CLK_ACT_C );
      scsbOb       <= ( others => '1' );

      case ( r.state ) is
         when IDLE =>
            v.phas    := not CLK_ACT_C;
            -- ws assertion implies cs is low
            if ( ws = '1' ) then
               if ( unsigned( shftDatIb(4 downto 0) ) < NUM_REGS_G ) then
                  v.op          := shftDatIb(7);
                  v.sel         := to_integer( unsigned( shftDatIb(4 downto 0) ) );
                  v.state       := DATA;
                  shftDatOb     <= r.regs( v.sel );
                  if ( v.op /= OP_READ_C ) then
                     -- phas is used to synchronize propagation
                     -- of the SPI signals with the SCLK
                     v.phas        := CLK_ACT_C;
                     scsbOb(v.sel) <= '0';
                  end if;
               else
                  v.state       := DRAIN;
               end if;
            end if;

         when DATA =>
            shftDatOb <= r.regs( r.sel );
            if ( r.op /= OP_READ_C ) then
               -- maintain CS asserted while waiting for the input clock
               -- to deassert; the clock is not propagated yet
               scsbOb(r.sel) <= '0';
               -- wait for clock to be deasserted until propagating SPI
               if ( (r.phas /= CLK_ACT_C) or (sclkIb /= CLK_ACT_C) ) then
                  propagateSPI := true;
                  v.phas       := not CLK_ACT_C;
               end if;
            end if;
            if ( ws = '1' ) then
               v.state := DRAIN;
               if ( r.op /= OP_READ_C ) then
                  v.regs( r.sel ) := shftDatIb;
                  v.phas          := CLK_ACT_C;
                  v.state         := DEASS_CS;
               end if;
            end if;

         when DEASS_CS =>
            propagateSPI := true;
            -- wait for clock to be deasserted
            if ( (r.phas /= CLK_ACT_C) or (sclkIb /= CLK_ACT_C) ) then
               -- stop SCLK to the target but keep CS asserted
               propagateSPI  := false;
               scsbOb(r.sel) <= scsbIb;
               v.phas        := not CLK_ACT_C;
               -- hang here until scsIb is deasserted (statement below)
            end if;

         when DRAIN =>
               -- wait until CS raising edge (see below)
      end case;

      if ( (scsbIb and not r.lscsb) = '1' ) then
         -- CSEL was just deasserted
         v.state := IDLE;
      end if;

      if ( propagateSPI ) then
         sclkOb(r.sel) <= sclkIb;
         scsbOb(r.sel) <= '0';
      end if;

      rin          <= v;
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

end architecture rtl;
