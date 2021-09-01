library ieee;

use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity Ft240FifoTb is
end entity Ft240FifoTb;

architecture rtl of Ft240FifoTb is
   constant CLOCK_FREQ_C  : real := 24.0E6;

   signal clk : std_logic := '0';
   signal rst : std_logic := '0';
   signal run : boolean   := true;

   signal datInp : std_logic_vector(7 downto 0) := x"54";
   signal datOut : std_logic_vector(7 downto 0) := x"54";
   signal rdb    : std_logic := '1';
   signal wr     : std_logic := '0';

   signal rxe    : std_logic := '0';
   signal txf    : std_logic := '0';

   signal rrdy   : std_logic := '0';
   signal rvld   : std_logic := '0';
   signal rdat   : std_logic_vector(7 downto 0);

   signal wrdy   : std_logic := '0';
   signal wvld   : std_logic := '0';
   signal wdat   : std_logic_vector(7 downto 0);

   signal tdat   : std_logic_vector(7 downto 0) := (others => 'X');

   type   FifoType is record
      data : std_logic_vector(7 downto 0);
      full : std_logic;
      empt : std_logic;
      lstR : std_logic;
      lstW : std_logic;
   end record FifoType;

   constant FIFO_INIT_C : FifoType := (
      data => ( others => 'X' ),
      full => '0',
      empt => '1',
      lstR => '1',
      lstW => '0'
   );

   signal fifo   : FifoType := FIFO_INIT_C;
   signal fifoIn : FifoType;

   procedure fifoTx(
      signal   do : out std_logic_vector; 
      signal   vl : out std_logic;
      constant di : in  std_logic_vector
   ) is
   begin
      do <= di;
      vl <= '1';
      wait until rising_edge( clk );
      while ( (wvld and wrdy) = '0' ) loop
         wait until rising_edge( clk );
      end loop;
      vl <= '0';
      wait for 1 ns;
   end procedure fifoTx;

   procedure fifoRx(
      signal   do : out std_logic_vector; 
      signal   rd : out std_logic
   ) is
   begin
      rd <= '1';
      wait until rising_edge( clk );
      while ( (rvld and rrdy) = '0' ) loop
         wait until rising_edge( clk );
      end loop;
      do <= rdat;
      rd <= '0';
      wait for 1 ns;
   end procedure fifoRx;

   -- simulate RXE/TXF assertion immediately after RDb/WE becoming inactive
   signal rdLockout : std_logic := '0';
   signal wrLockout : std_logic := '0';

   signal rdStart   : time      := 0 ns;
   signal wrStart   : time      := 0 ns;

   signal numTx     : integer   := 0;
   signal numRx     : integer   := 0;

begin

   P_FIFO_COMB : process (fifo, wr, rdb, datOut) is
      variable v : FifoType;
   begin
      v := fifo;

      v.lstR := rdb;
      v.lstW := wr;

      assert not ( (wr = '1') and ( rdb = '0' ) )
         report "concurrent RD/WR cycle: RDb " & std_logic'image(rdb) & " WR " & std_logic'image(wr)
         severity failure;

      if ( (rdb = '0') and (fifo.lstR = '1') ) then
        rdStart <= now;
      end if;

      if ( (wr  = '1') and (fifo.lstW = '0') ) then
        wrStart <= now;
      end if;

      if ( (rdb = '1') and (fifo.lstR = '0') ) then
        -- read cycle
        assert fifo.empt = '0' report "reading empty fifo" severity failure;

        assert now - rdStart >= 50 ns report "Short read cycle" severity failure;
        v.empt := '1';
        v.full := '0';
        rdLockout <= '1' after 25 ns;
      else
        rdLockout <= '0';
      end if;

      if ( (wr  = '0') and (fifo.lstW = '1') ) then
        -- write cycle
        assert fifo.full = '0' report "writing full fifo" severity failure;

        assert now - wrStart >= 50 ns report "Short write cycle: " & time'image(now - wrStart) severity failure;
        v.full := '1';
        v.empt := '0';
        v.data := datOut;
        wrLockout <= '1' after 25 ns;
      else
        wrLockout <= '0';
      end if;

      fifoIn <= v;
   end process P_FIFO_COMB;

   datInp <= fifo.data;
   rxe    <= fifo.empt or rdLockout;
   txf    <= fifo.full or wrLockout;

   P_CLK : process is
   begin
      if ( run ) then
         wait for (1.0/CLOCK_FREQ_C/2.0) * 1 sec;
         clk <= not clk;
      else
         wait;
      end if;
   end process P_CLK;

   P_CONS : process is
      variable got  : integer;
      variable exp  : integer;
   begin
      exp := numRx;
      
      fifoRx(tdat, rrdy);
      -- throttle
      if ( exp < 4 ) then
         for i in 1 to 50 loop
            wait until rising_edge( clk );
         end loop;
      end if;
      got := integer(to_integer(unsigned(tdat)));
      report integer'image(got);
      assert got = exp report "Expected: " & integer'image(exp) severity failure;
      numRx <= numRx + 1;
      wait for 1 ns;
   end process P_CONS;

   P_PROD : process is 
      variable v : std_logic_vector(7 downto 0);
      variable j : integer;
   begin
      j := 0;

      for i in 1 to 20 loop
         wait until rising_edge( clk );
      end loop;

-- send some values back to back
      for i in 1 to 8 loop
        fifoTx( wdat, wvld, std_logic_vector(to_unsigned(j, 8)));
        j := j + 1;
      end loop;

-- trottle write
      for i in 1 to 8 loop
        fifoTx( wdat, wvld, std_logic_vector(to_unsigned(j, 8)));
        j := j + 1;
        for k in 1 to 100 loop
           wait until rising_edge( clk );
        end loop;
      end loop;
     
      for i in 1 to 20 loop
         if ( numRx = j ) then
            report "Test PASSED";
            run <= false;
            wait;
         end if;
         wait until rising_edge( clk );
      end loop;

      report "not all items received - timeout" severity failure;
      wait;
   end process P_PROD;

   U_DUT : entity work.Ft240Fifo
      generic map (
         CLOCK_FREQ_G => CLOCK_FREQ_C
      )
      port map (
         clk     => clk,
         rst     => rst,

         fifoRXE => rxe,
         fifoRDT => datInp,
         fifoRDb => rdb,

         fifoTXF => txf,
         fifoWDT => datOut,
         fifoWR  => wr,

         rdat    => rdat,
         rvld    => rvld,
         rrdy    => rrdy,

         wdat    => wdat,
         wvld    => wvld,
         wrdy    => wrdy
      );

   P_SEQ : process ( clk ) is
   begin
      if ( rising_edge( clk ) ) then
        if ( rst = '1' ) then
           fifo <= FIFO_INIT_C;
        else
           fifo <= fifoIn;
        end if;
     end if;
   end process P_SEQ;

end architecture rtl;
