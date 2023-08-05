library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;
use ieee.math_real.all;

use work.BasicPkg.all;
use work.CommandMuxPkg.all;

-- simple memory mapped access
--
--  READ REQUEST:
--    command byte followed by an address byte and a count byte; the count is
--    the actual count minus one (e.g., 0 represents a count of 1).
--
--       cmd, addr, count
--
--  READ REPLY:
--    command byte (echo) followed by count + 1 data bytes and a termiating status
--    byte.
--
--       cmd, d0, ..., status
--
--    status is 0 on success and nonzero if a an error occurred (note that incomplete
--    requests also result in an error reply - without any data).
--    The application may flag a read error by asserting 'err'.
--
--  WRITE REQUEST:
--    command byte followed by an address byte and one or more data bytes.
--
--       cmd, addr, d0, ..., dn
--
--  WRITE REPLY:
--    command byte (echo) followed by a status byte
--
--       cmd, status
--
--    nonzero status indicates a write error flagged by the application or
--    an invalid command (e.g., missing address).
--
--  APPLICATION DATA PORT:
--
--    addr, rdnw ('1' for read, '0' for write) and wdat (if rdnw='0') qualified by 'vld=1'
--
--    the application responds by asserting 'rdy' and providing 'rdat' (if rdnw='1') and
--    status (err='0' success, err='1' error; this leads to the entire request being
--    aborted).
--    Data are transferred during the cycle when 'vld' and 'rdy' are both asserted.

entity CommandReg is
   generic (
      ADDR_W_G     : natural := 8;
      DATA_W_G     : natural := 8
   );
   port (
      clk          : in  std_logic;
      rst          : in  std_logic;

      mIb          : in  SimpleBusMstType;
      rIb          : out std_logic;

      mOb          : out SimpleBusMstType;
      rOb          : in  std_logic;

      addr         : out unsigned(ADDR_W_G - 1 downto 0);
      wdat         : out std_logic_vector(DATA_W_G - 1 downto 0);
      rdat         : in  std_logic_vector(DATA_W_G - 1 downto 0);
      rdnw         : out std_logic;
      vld          : out std_logic;
      rdy          : in  std_logic := '1';
      err          : in  std_logic := '0'
   );
end entity CommandReg;

architecture rtl of CommandReg is

   type StateType is (IDLE, A8, L8, RD8, WR8, STA, DRAIN, REPLY);

   type RegType is record
      state        : StateType;
      addr         : unsigned(ADDR_W_G - 1 downto 0);
      count        : unsigned(ADDR_W_G     downto 0);
      cmd          : std_logic_vector(7 downto 0);
      err          : std_logic;
   end record RegType;

   constant REG_INIT_C : RegType := (
      state        => IDLE,
      addr         => (others => '0'),
      count        => (others => '1'),
      cmd          => (others => '0'),
      err          => '0'
   );

   signal r        : RegType := REG_INIT_C;
   signal rin      : RegType := REG_INIT_C;
begin

   P_COMB : process ( r, mIb, rOb, rdat, rdy, err ) is
      variable v : RegType;
   begin
      v          := r;
      mOb        <= SIMPLE_BUS_MST_INIT_C;
      mOb.dat    <= r.cmd;
      mOb.lst    <= '0';
      mOb.vld    <= '0';
      rIb        <= '0';
      rdnw       <= '1';
      vld        <= '0';

      case ( r.state ) is
         when IDLE  =>
            rIb     <= '1';
            v.count := (others => '1');
            v.err   := '1';
            if ( mIb.vld = '1' ) then
               v.cmd := mIb.dat;
               if (   subCommandRegGet( mIb.dat ) = CMD_REG_RD8_C
                   or subCommandRegGet( mIb.dat ) = CMD_REG_WR8_C ) then
                     v.state := A8;
               else
                     v.state := DRAIN;
               end if;
               if ( mIb.lst = '1' ) then
                  v.state := REPLY;
               end if;
            end if;

         when A8    =>
            rIb <= '1';
            if ( mIb.vld = '1' ) then
               v.addr := resize( unsigned( mIb.dat ), v.addr'length );
               if ( subCommandRegGet( r.cmd ) = CMD_REG_RD8_C ) then
                  v.state := L8;
               else
                  v.state := WR8;
               end if;
               if ( mIb.lst = '1' ) then
                  v.state := REPLY;
               end if;
            end if;

         when L8    =>
            rIb <= '1';
            if ( mIb.vld = '1' ) then
               if ( mIb.lst = '1' ) then
                  v.count := resize( unsigned(mIb.dat), v.count'length );
                  v.state := REPLY;
               else
                  v.state := DRAIN;
               end if;
            end if;

         when WR8   =>
            rdnw <= '0';
            rIb  <= rdy;
            vld  <= mIb.vld;
            if ( (mIb.vld and rdy ) = '1' ) then
               v.addr := r.addr + 1;
               v.err  := err;
               if    ( mIb.lst = '1' ) then
                  v.state := REPLY;
               elsif ( err     = '1' ) then
                  v.state := DRAIN;
               end if;
            end if;

         when RD8   =>
            vld     <= rOb; 
            mOb.dat <= rdat; -- needs rework if DATA_W_G /= 8
            mOb.vld <= rdy and not err;
            if ( (rOb and rdy) = '1' ) then
               v.addr  := r.addr  + 1;
               v.count := r.count - 1;
               v.err   := err;
               if ( (err or v.count(v.count'left)) = '1' ) then
                  v.state := STA;
               end if;
            end if;

         when STA   =>
            mOb.dat <= (others => r.err);
            mOb.vld <= '1';
            mOb.lst <= '1';
            if ( rOb = '1' ) then
               v.state := IDLE;
            end if;

         when DRAIN =>
            rIb <= '1';
            if ( (mIb.vld and mIb.lst) = '1' ) then
               v.state := REPLY;
            end if;

         when REPLY =>
            mOb.vld <= '1';
            -- reply (CMD echo)
            if ( rOb = '1' ) then
               if ( r.count(r.count'left) = '0' ) then
                  -- successfully latched a count; this must be a read op
                  v.state := RD8;
               else
                  v.state := STA;
               end if;
            end if;
      end case;

      rin        <= v;
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

   addr <= r.addr;
   wdat <= mIb.dat; -- needs rework if DATA_W_G /= 8

end architecture rtl;