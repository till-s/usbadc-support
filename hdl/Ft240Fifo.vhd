library ieee;
use     ieee.std_logic_1164.all;
use     ieee.math_real.all;
use     ieee.numeric_std.all;

entity Ft240Fifo is
   generic (
      CLOCK_FREQ_G : real := 24.0E6
   );
   port (
      clk     : in  std_logic;
      rst     : in  std_logic := '0';

      fifoRXE : in  std_logic := '1';
      fifoRDT : in  std_logic_vector(7 downto 0);
      fifoRDb : out std_logic;

      fifoIOT : out std_logic;

      fifoTXF : in  std_logic := '1';
      fifoWDT : out std_logic_vector(7 downto 0) := x"AA";
      fifoWR  : out std_logic;

      rdat    : out std_logic_vector(7 downto 0);
      rvld    : out std_logic;
      rrdy    : in  std_logic := '0';

      wdat    : in  std_logic_vector(7 downto 0) := (others => '0');
      wvld    : in  std_logic := '0';
      wrdy    : out std_logic
   );
end entity Ft240Fifo;

architecture rtl of Ft240Fifo is

   function max(constant a,b : integer) return integer is
   begin
      if ( a > b ) then return a; else return b; end if;
   end function max;

   constant SYNC_STAGES_C        : positive := 2;

   constant RD_PULSE_WIDTH_MIN_C : real := 50.0E-9 * CLOCK_FREQ_G;

   -- read-active -> data valid max: 50ns => zero setup time
   -- make the pulse wider by the min. sestup time (very conservative).
   constant RD_SETUP_MIN_C       : real := 20.0E-9;

   -- count from RD_PULSE_WIDTH_C downto 0 -> subtract 1
   constant RD_PULSE_WIDTH_C     : integer := integer( ceil( RD_SETUP_MIN_C + RD_PULSE_WIDTH_MIN_C ) ) - 1;

   -- RXE needs some time to change after a readout; plus, the signal
   -- has to trickle through the synchronizer
   -- TXF actually has identical timing requirements; thus, we use
   -- the same logic for generating cycles;
   -- count from HANDSHAKE_INVAL_C downto 0 -> subtract 1; handshakeStable is registered -> subtrace an extra 1
   constant HANDSHAKE_INVAL_C    : integer := integer( ceil( 25.0E-9 * CLOCK_FREQ_G ) ) + SYNC_STAGES_C - 2;

   constant RD_CNT_LD_C          : integer := integer( floor( log2( real( max( RD_PULSE_WIDTH_C, HANDSHAKE_INVAL_C ) ) ) ) ) + 1;

   -- subtract one cycle because handshakeStable is registered
   constant HSK_STBL_CNT_C       : unsigned(RD_CNT_LD_C - 1 downto 0) := to_unsigned( HANDSHAKE_INVAL_C, RD_CNT_LD_C );

   type   MuxState        is ( IDLE, READ, WRITE );

   type   RegType         is  record
      rdataBuf        : std_logic_vector(7 downto 0);
      wdataBuf        : std_logic_vector(7 downto 0);
      rdataVld        : std_logic;
      wdataRdy        : std_logic;

      dly             : unsigned(RD_CNT_LD_C - 1 downto 0);
      pulse           : std_logic;
      handshakeStable : std_logic;

      pulseMux        : MuxState;
   end record RegType;

   constant REG_INIT_C : RegType := (
      rdataBuf        => (others => 'X'),
      wdataBuf        => (others => 'X'),
      rdataVld        => '0',
      wdataRdy        => '1',

      dly             => HSK_STBL_CNT_C,
      pulse           => '1',
      handshakeStable => '0',

      pulseMux        => IDLE
   );

   signal r               : RegType := REG_INIT_C;
   signal rin             : RegType;

   -- direction control of the data I/O buffer
   signal iot             : std_logic := '1';

   signal rxeSyn          : std_logic;
   signal txfSyn          : std_logic;

   signal rdb             : std_logic;
   signal wr              : std_logic;

begin

   U_RXE_TXF_SYNC : entity work.SynchronizerBit
      generic map (
         STAGES_G => SYNC_STAGES_C,
         WIDTH_G  => 2,
         RSTPOL_G => '1'
      )
      port map (
         clk       => clk,
         rst       => rst,
         datInp(0) => fifoRXE,
         datInp(1) => fifoTXF,
         datOut(0) => rxeSyn,
         datOut(1) => txfSyn
      );

   -- slow but simple...

   P_COMB : process ( r, rrdy, wvld, wdat, fifoRDT, rxeSyn, txfSyn ) is
      variable v : RegType;
   begin
      v := r;

      -- deliver rdata
      if ( (r.rdataVld and rrdy) = '1' ) then
         v.rdataVld := '0';
      end if;

      -- buffer wdata
      if ( (r.wdataRdy and wvld) = '1' ) then
         v.wdataRdy := '0';
         v.wdataBuf := wdat;
      end if;

      -- handle timeouts
      if ( r.dly = 0 ) then
         if ( r.pulse = '0' ) then
            v.pulse           := '1';
            if ( r.pulseMux = READ ) then
               v.rdataBuf     := fifoRDT;
               v.rdataVld     := '1';
            else
               v.wdataRdy     := '1';
            end if;
            v.handshakeStable := '0';
            v.dly             := HSK_STBL_CNT_C;
         else
            v.handshakeStable := '1';
            v.pulseMux        := IDLE;
         end if;
      else
         v.dly  := r.dly - 1;
      end if;

      -- issue a cycle to the FIFO

      if (     r.handshakeStable = '1'
           and r.pulse           = '1'
           and r.pulseMux        = IDLE ) then

         if    ( (r.wdataRdy = '0') and (txfSyn = '0') ) then
            -- write over read
            v.pulseMux := WRITE;
         elsif ( (r.rdataVld = '0') and (rxeSyn = '0') ) then
            v.pulseMux := READ;
         end if;

         if ( v.pulseMux /= IDLE ) then
            v.pulse := '0';
            v.dly   := to_unsigned(RD_PULSE_WIDTH_C - 1, r.dly'length);
         end if;
      end if;

      rin <= v;
   end process P_COMB;

   P_SEQ : process ( clk ) is
   begin
      if ( rising_edge( clk ) ) then
         if ( rst = '1') then
            r <= REG_INIT_C;
         else
            r <= rin;
         end if;
      end if;
   end process P_SEQ;

   P_MUX : process( r ) is
   begin
      if ( r.pulseMux = WRITE ) then
         wr        <= not r.pulse;
         rdb       <= '1';
      else
         rdb       <= r.pulse;
         wr        <= '0';
      end if;
   end process P_MUX;

   rdat    <= r.rdataBuf;
   rvld    <= r.rdataVld;
   fifoRDb <= rdb;

   wrdy    <= r.wdataRdy;

   fifoWR  <= wr;
   fifoWDT <= r.wdataBuf;
   fifoIOT <= not wr;

end architecture rtl;
