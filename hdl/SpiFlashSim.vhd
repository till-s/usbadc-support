library ieee;
use     ieee.std_logic_1164.all;
use     ieee.numeric_std.all;

entity SpiFlashSim is
   generic (
      --     { description: "AT25SL641",  id: 0x1f43171f43ULL, blockSize: 4096, pageSize: 256, sizeBytes: 8*1024*1024 }

      FLASH_ID_G     : std_logic_vector(39 downto 0) := x"deadbeef00"
   );
   port (
      clk            : in  std_logic;
      sclk           : in  std_logic;
      scsb           : in  std_logic;
      mosi           : in  std_logic;
      miso           : out std_logic
   );
end entity SpiFlashSim;

architecture sim of SpiFlashSim is
   type StateType is (IDLE, A2, A1, A0, SKIP, READ, PGWR, WR_STATUS, WAI, ID);

   constant MEM_SZ_C       : natural := 65536;
   constant PG_SZ_C        : natural := 256;

   type MemArray is array (natural range <>) of std_logic_vector(7 downto 0);

   signal mem : MemArray(0 to MEM_SZ_C - 1) := (others => (others => '1'));

   constant OP_PAGE_WR_C   : std_logic_vector(7 downto 0) := x"02";
   constant OP_FAST_RD_C   : std_logic_vector(7 downto 0) := x"0b";
   constant OP_STATUS_RD_C : std_logic_vector(7 downto 0) := x"05";
   constant OP_WRITE_ENA_C : std_logic_vector(7 downto 0) := x"06";
   constant OP_WRITE_DIS_C : std_logic_vector(7 downto 0) := x"04";
   constant OP_STATUS_WR_C : std_logic_vector(7 downto 0) := x"01";
   constant OP_ERASE_4K_C  : std_logic_vector(7 downto 0) := x"20";
   constant OP_ERASE_32K_C : std_logic_vector(7 downto 0) := x"52";
   constant OP_ERASE_64K_C : std_logic_vector(7 downto 0) := x"D8";
   constant OP_RESUME_C    : std_logic_vector(7 downto 0) := x"AB";
   constant OP_ID_C        : std_logic_vector(7 downto 0) := x"9F";

   type RegType is record
      state        : StateType;
      lscsb        : std_logic;
      lsclk        : std_logic;
      lmosi        : std_logic;
      dat_out      : std_logic_vector(7 downto 0);
      op           : std_logic_vector(7 downto 0);
      status       : std_logic_vector(7 downto 0);
      addr         : unsigned(23 downto 0);
      pgbuf        : MemArray(0 to PG_SZ_C - 1);
      pgptr        : unsigned(7 downto 0);
      count        : integer;
   end record RegType;

   constant REG_INIT_C : RegType := (
      state        => IDLE,
      lscsb        => '1',
      lsclk        => '0',
      lmosi        => '0',
      dat_out      => (others => '0'),
      op           => (others => '0'),
      status       => x"00",
      addr         => (others => '0'),
      pgbuf        => (others => (others => '0')),
      pgptr        => (others => '0'),
      count        => 0
   );

   signal r        : RegType := REG_INIT_C;
   signal rin      : RegType := REG_INIT_C;

   signal rs, ws   : std_logic;

   signal dat_inp  : std_logic_vector(7 downto 0);

begin

   P_COMB : process ( r, scsb, sclk, mosi, rs, ws, dat_inp, mem ) is
      variable v : RegType;
   begin
      v       := r;
      v.lscsb := scsb;
      v.lsclk := sclk;
      v.lmosi := mosi;

      case ( r.state ) is
         when WAI  => -- wait for CS deassertion
         when IDLE =>
            if ( ws = '1' ) then
               v.op := dat_inp;
               if    ( dat_inp = OP_PAGE_WR_C   ) then
                  v.state := A2;
               elsif ( dat_inp = OP_FAST_RD_C   ) then
                  v.state := A2;
               elsif ( dat_inp = OP_WRITE_ENA_C ) then
                  v.status(1) := '1';
                  v.state     := WAI;
               elsif ( dat_inp = OP_WRITE_DIS_C ) then
                  v.status(1) := '0';
                  v.state     := WAI;
               elsif ( dat_inp = OP_STATUS_RD_C ) then
                  v.state   := WAI;
                  v.dat_out := r.status;
               elsif ( dat_inp = OP_STATUS_WR_C ) then
                  v.state := WR_STATUS;
               elsif ( dat_inp = OP_ERASE_4K_C  ) then
                  v.state := A2;
               elsif ( dat_inp = OP_ERASE_32K_C ) then
                  v.state := A2;
               elsif ( dat_inp = OP_ERASE_64K_C ) then
                  v.state := A2;
               elsif ( dat_inp = OP_RESUME_C    ) then
                  -- no-op
                  v.state := WAI;
               elsif ( dat_inp = OP_ID_C    ) then
                  -- no-op
                  v.dat_out := FLASH_ID_G(4*8 + 7 downto 4*8);
                  v.state := ID;
                  v.count := 4-1; -- 0 based
               else
                  assert false report "Unsupported flash op " & integer'image( to_integer( unsigned( dat_inp ) ) ) severity failure;
               end if;
             end if;
         when WR_STATUS =>
           if ( ws = '1' ) then
              v.status := dat_inp;
              v.state  := WAI;
           end if;

         when A2 =>
           if ( ws = '1' ) then
              v.addr(23 downto 16) := unsigned(dat_inp);
              v.state := A1;
           end if;
         when A1 =>
           if ( ws = '1' ) then
              v.addr(15 downto  8) := unsigned(dat_inp);
              v.state := A0;
           end if;
         when A0 =>
           if ( ws = '1' ) then
              v.addr( 7 downto  0) := unsigned(dat_inp);
              if ( r.op = OP_FAST_RD_C ) then
                 v.state := SKIP;
              elsif ( r.op = OP_PAGE_WR_C ) then
                 v.pgptr := unsigned(dat_inp);
                 for i in 0 to PG_SZ_C - 1 loop
                    v.pgbuf(i) := mem( to_integer( r.addr(23 downto 16) )*256 + i );
                 end loop;
                 v.state := PGWR;
              else
                 v.state := A0;
              end if;
           end if;
         when SKIP =>
           if ( ws = '1' ) then
              v.state   := READ;
              v.dat_out := mem( to_integer( r.addr ) );
              v.addr    := r.addr + 1;
           end if;

         when ID   =>
           if ( rs = '1' ) then
              v.dat_out := FLASH_ID_G(r.count*8 + 7 downto r.count*8);
              if ( r.count = 0 ) then
                 v.state := WAI;
              else
                 v.count := r.count - 1;
              end if;
           end if;

         when READ =>
           if ( rs = '1' ) then
              v.dat_out := mem( to_integer( r.addr ) );
              v.addr    := r.addr + 1;
           end if;

         when PGWR =>
           if ( ws = '1' ) then
              v.pgbuf(to_integer(r.pgptr)) := dat_inp;
              v.pgptr := r.pgptr + 1;
           end if;
      end case;

      if ( (scsb and not r.lscsb) = '1' ) then
         v.state   := IDLE;
         v.dat_out := x"ff";
      end if;

      rin     <= v;
   end process P_COMB;

   P_SEQ : process ( clk ) is
      function bas(constant x : in RegType; constant msk : in unsigned(23 downto 0)) return natural is
      begin
         return to_integer( x.addr and msk );
      end function bas;
   begin
      if ( rising_edge( clk ) ) then
         r <= rin;
         if ( (scsb and not r.lscsb and r.status(1)) = '1' ) then
            if    ( r.op = OP_PAGE_WR_C ) then
               for i in 0 to PG_SZ_C - 1 loop
                  mem( bas(r, x"ffff00") + i ) <= r.pgbuf(i);
               end loop;
            elsif ( r.op = OP_ERASE_4K_C ) then
               for i in 0 to 4095 loop
                  mem( bas(r, x"fff000") + i ) <= x"ff";
               end loop;
            elsif ( r.op = OP_ERASE_32K_C ) then
               for i in 0 to 32767 loop
                  mem( bas(r, x"ff8000") + i ) <= x"ff";
               end loop;
            elsif ( r.op = OP_ERASE_64K_C ) then
               for i in 0 to 65535 loop
                  mem( bas(r, x"ff0000") + i ) <= x"ff";
               end loop;
            end if;
         end if;
      end if;
   end process P_SEQ;

   U_SR : entity work.SpiReg
      generic map (
         FRAMED_G  => true
      )
      port map (
         clk       => clk,
         sclk      => sclk,
         scsb      => scsb,
         mosi      => mosi,
         miso      => miso,

         data_inp  => r.dat_out,
         rs        => rs,

         data_out  => dat_inp,
         ws        => ws
      );

end architecture sim;

