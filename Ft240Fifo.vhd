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

   constant RD_PULSE_WIDTH_C     : integer := integer( ceil( RD_SETUP_MIN_C + RD_PULSE_WIDTH_MIN_C ) ); 

   -- RXE needs some time to change after a readout; plus, the signal
   -- has to trickle through the synchronizer
   -- TXF actually has identical timing requirements; thus, we use
   -- the same logic for generating cycles;
   constant HANDSHAKE_INVAL_C    : integer := integer( ceil( 25.0E-9 * CLOCK_FREQ_G ) ) + SYNC_STAGES_C;

   constant RD_CNT_LD_C          : integer := integer( ceil( log2( real( max( RD_PULSE_WIDTH_C, HANDSHAKE_INVAL_C ) ) ) ) );

   -- subtract one cycle because handshakeStable is registered
   constant HSK_STBL_CNT_C       : unsigned(RD_CNT_LD_C - 1 downto 0) := to_unsigned( HANDSHAKE_INVAL_C - 2, RD_CNT_LD_C );

   type   MuxState        is ( IDLE, READ, WRITE );

   signal rdataBuf        : std_logic_vector(7 downto 0);
   signal wdataBuf        : std_logic_vector(7 downto 0);
   signal rdataVld        : std_logic := '0';
   signal wdataRdy        : std_logic := '1';

   signal dly             : unsigned(RD_CNT_LD_C - 1 downto 0) := HSK_STBL_CNT_C;
   signal pulse           : std_logic := '1';
   signal handshakeStable : std_logic := '0';

   signal pulseMux        : MuxState  := IDLE;

   -- direction control of the data I/O buffer
   signal iot             : std_logic := '1';

   signal rxeSyn          : std_logic;
   signal txfSyn          : std_logic;

   signal handshake       : std_logic;
   signal rdb             : std_logic;
   signal wr              : std_logic;

   component chipscope_ila is
      PORT (
         CONTROL : INOUT STD_LOGIC_VECTOR(35 DOWNTO 0);
         CLK     : IN STD_LOGIC;
         TRIG0   : IN STD_LOGIC_VECTOR(7 DOWNTO 0);
         TRIG1   : IN STD_LOGIC_VECTOR(7 DOWNTO 0);
         TRIG2   : IN STD_LOGIC_VECTOR(7 DOWNTO 0)
      );
   end component chipscope_ila;

   component chipscope_icon is
      PORT (
         control0 : out std_logic_vector(35 downto 0)
      );
   end component chipscope_icon;

   signal ila_trg0 : std_logic_vector( 7 downto 0) := (others => '0');
   signal ila_trg1 : std_logic_vector( 7 downto 0) := (others => '0');
   signal ila_trg2 : std_logic_vector( 7 downto 0) := (others => '0');
   signal ila_ctrl : std_logic_vector(35 downto 0);

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

   P_SEQ : process ( clk ) is
   begin
      if ( rising_edge( clk ) ) then
         if ( rst = '1') then
            rdataVld        <= '0';
            wdataRdy        <= '1';
            pulse           <= '1';
            dly             <= HSK_STBL_CNT_C;
            handshakeStable <= '0';
         else
            if ( dly /= 0 ) then
               dly  <= dly - 1;
            end if;

            -- deliver rdata
            if ( (rdataVld and rrdy) = '1' ) then
               rdataVld <= '0';
            end if;

            -- buffer wdata 
            if ( (wdataRdy and wvld) = '1' ) then
               wdataRdy <= '0';
               wdataBuf <= wdat;
            end if;

            -- issue a cycle to the FIFO

            if ( rdataVld = '0' or wdataRdy = '0' ) then
               if ( pulseMux = IDLE ) then
                  if ( rdataVld = '0' ) then
                     -- prioritize read over write
                     pulseMux <= READ;
                  else
                     pulseMux <= WRITE;
                  end if;
               end if;
               if ( (pulse and handshakeStable) = '1' ) then
                  if ( rxeSyn = '0' ) then
                     pulse <= '0';
                     dly   <= to_unsigned(RD_PULSE_WIDTH_C - 1, dly'length);
                  end if;
               else
                  if ( dly = 0 ) then
                     if ( pulse = '0' ) then
                        pulse           <= '1';
                        if ( pulseMux = READ ) then
                           rdataBuf        <= fifoRDT;
                           rdataVld        <= '1';
                        else
                           wdataRdy        <= '1';
                        end if;
                        handshakeStable <= '0';
                        dly             <= HSK_STBL_CNT_C;
                     else
                        handshakeStable <= '1';
                        pulseMux        <= IDLE;
                     end if;
                  end if;
               end if;
            end if;
         end if;
      end if;
   end process P_SEQ;

   P_MUX : process( pulseMux, rxeSyn, txfSyn, pulse ) is
   begin
      if ( pulseMux = WRITE ) then
         wr        <= not pulse;
         handshake <= txfSyn;
         rdb       <= '1';
      else
         rdb       <= pulse;
         handshake <= rxeSyn;
         wr        <= '0';
      end if;
   end process P_MUX;

   rdat    <= rdataBuf;
   rvld    <= rdataVld;
   fifoRDb <= rdb;

   wrdy    <= wdataRdy;

   fifoWR  <= wr;
   fifoWDT <= wdataBuf;
   fifoIOT <= not wr;

   ila_trg0(0) <= fifoRXE;
   ila_trg0(1) <= rdb;
   ila_trg0(2) <= rdataVld;
   ila_trg0(3) <= rrdy;

   ila_trg0(4) <= fifoTXF;
   ila_trg0(5) <= wr;
   ila_trg0(6) <= wvld;
   ila_trg0(7) <= wdataRdy;

   U_ICON : component chipscope_icon
      port map (
         control0 => ila_ctrl
      );

   U_ILA : component chipscope_ila
      port map(
         CONTROL => ila_ctrl,
         CLK     => clk,
         TRIG0   => ila_trg0,
         TRIG1   => ila_trg1,
         TRIG2   => ila_trg2
      );


end architecture rtl;
