library ieee;
use     ieee.std_logic_1164.all;
use     ieee.numeric_std.all;
use     ieee.math_real.all;

use     work.BasicPkg.all;

entity SpiBitShifter is
  generic (
    WIDTH_G : natural  := 8;
    DIV2_G  : positive := 1; -- half-period of pre-scaler
    -- delay from cs-lo to clock
    CSLO_G  : natural  := 0; -- 0 -> same as DIV2_G
    -- min cs high 
    CSHI_G  : natural  := 0; -- 0 -> same as DIV2_G
    -- delay cs after negedge of SCLK (0: no delay)
    CSDL_G  : natural  := 8
  );
  port (
    clk     : in  std_logic;
    rst     : in  std_logic;

    -- parallel data in
    datInp  : in  std_logic_vector(WIDTH_G - 1 downto 0);
    -- chip-select; if deasserted (high) nothing
    -- will be shifted
    csbInp  : in  std_logic;
    vldInp  : in  std_logic;
    rdyInp  : out std_logic;

    -- parallel data out
    datOut  : out std_logic_vector(WIDTH_G - 1 downto 0);
    -- valid/ready handshake
    vldOut  : out std_logic;
    rdyOut  : in  std_logic := '1';

    serClk  : out std_logic;
    serCsb  : out std_logic;
    serInp  : in  std_logic;
    serOut  : out std_logic
  );
end entity SpiBitShifter;

architecture Impl of SpiBitShifter is

  constant LD_W_C : natural := natural( ceil( log2( real( WIDTH_G ) ) ) ) + 1;
  type StateType is ( IDLE, CHIPSELB, SHIFT, DONE );

  constant SCLK_INACTIVE_C : std_logic := '0';

  function max(a,b: integer)
    return integer is
  begin
    return ite( a > b, a, b );
  end function max;

  constant PRHI_C : natural := max( max(DIV2_G, CSLO_G), max( CSHI_G, CSDL_G ) );

  type RegType is record
    state    : StateType;
    clkCnt   : unsigned(LD_W_C - 1 downto 0);
    sreg     : std_logic_vector(WIDTH_G downto 0);
    scsb     : std_logic;
    prsc     : natural range 0 to PRHI_C - 1;
    sclk     : std_logic;
  end record RegType;

  constant REG_INIT_C : RegType := (
    state    => IDLE,
    clkCnt   => (others => '0'),
    sreg     => (others => '1'),
    scsb     => '1',
    sclk     => SCLK_INACTIVE_C,
    prsc     => 0
  );

  signal  r   : RegType := REG_INIT_C;
  signal  rin : RegType;

  constant CSLO_C : positive := ite( CSLO_G = 0, DIV2_G, CSLO_G );
  constant CSHI_C : positive := ite( CSHI_G = 0, DIV2_G, CSHI_G );

begin

  P_COMB : process ( r, datInp, csbInp, serInp, vldInp, rdyOut ) is
    variable v         : RegType;
    variable vldOutLoc : std_logic;
  begin
    v := r;
    if ( r.prsc > 0 ) then
      v.prsc := r.prsc - 1;
    end if;
    vldOutLoc := '0';
    rdyInp    <= '0';
    case ( r.state ) is
      when IDLE =>
        rdyInp <= '1';
        if ( vldInp = '1' ) then
          -- if chip-select changes then handle this first
          v.sreg   := datInp & '0';
          v.prsc   := DIV2_G - 1;
          v.scsb   := csbInp;
          v.sclk   := SCLK_INACTIVE_C;
          v.clkCnt := to_unsigned(2*WIDTH_G - 1, v.clkCnt'length);
          if ( csbInp = '0' ) then
            v.state   := SHIFT;
            if ( r.scsb = '1' ) then
               v.prsc    := CSLO_C - 1;
            else
               v.prsc    := DIV2_G - 1;
            end if;
          else
            v.state := CHIPSELB;
            v.prsc  := CSHI_C - 1;
            if ( CSDL_G > 0 ) then
              v.scsb := '0';
              -- delay CHIPSELB for required time before deasserting
              v.prsc := CSDL_G - 1;
            end if;
          end if;
        end if;

      when CHIPSELB =>
        if ( r.prsc = 0 ) then
          if ( r.scsb = '1' ) then
            v.state   := DONE;
            vldOutLoc := '1';
          else
            -- hold CHIPSELB for required time before accepting a new command
            v.scsb := '1';
            v.prsc := CSHI_C - 1;
          end if;
        end if;

      when SHIFT =>
        if ( r.prsc = 0 ) then
          v.sclk   := not r.sclk;
          v.clkCnt := r.clkCnt - 1;
          if ( r.clkCnt = 0 ) then
            vldOutLoc := '1';
            -- possible shortcut (see below)
            v.state   := DONE;
          else
            v.prsc := DIV2_G - 1;
          end if;
          if ( r.sclk = SCLK_INACTIVE_C ) then
            -- positive edge, register serial input
            v.sreg(0) := serInp;
          else
            -- negative edge, shift out
            v.sreg(r.sreg'left downto 1) := r.sreg( r.sreg'left - 1 downto 0 );
          end if;
        end if;

      when DONE =>
        vldOutLoc := '1';
    end case;

    vldOut <= vldOutLoc;

    -- from some states this shortcuts into IDLE
    if ( (vldOutLoc and rdyOut) = '1' ) then
       v.state := IDLE;
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

  -- assign outputs
  serClk <= r.sclk;
  serOut <= r.sreg( r.sreg'left );
  serCsb <= r.scsb;
  datOut <= r.sreg( datOut'range );
end architecture Impl;
