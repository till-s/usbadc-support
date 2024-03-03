library ieee;
use     ieee.std_logic_1164.all;
use     ieee.numeric_std.all;

use     work.BasicPkg.all;

entity SampleBuffer is
   generic (
      -- SDRAM address width
      A_WIDTH_G     : natural := 0;
      MEM_DEPTH_G   : natural := 0;
      D_WIDTH_G     : natural := 20
   );
   port (
      -- write side
      wrClk         : in  std_logic;
      wrEna         : in  std_logic;

      -- msb is 'command' flag
      wrDat         : in  std_logic_vector(D_WIDTH_G     downto 0);
      wrFul         : out std_logic := '0'; -- diagnostic signal (unused)

      -- UNUSED - for compatibility with SDRAM architecture only
      sdramClk      : in  std_logic := '0';
      sdramReq      : out std_logic := '0';
      sdramAck      : in  std_logic := '0';
      sdramAddr     : out std_logic_vector(A_WIDTH_G - 1  downto 0) := (others => '0');
      sdramRdnwr    : out std_logic := '0';
      sdramWDat     : out std_logic_vector(15 downto 0) := (others => '0');
      sdramRDat     : in  std_logic_vector(15 downto 0) := (others => '0');
      sdramRVld     : in  std_logic := '0';

      -- read side
      rdClk         : in  std_logic;
      rdEna         : in  std_logic;
      rdDat         : out std_logic_vector(D_WIDTH_G     downto 0);
      rdEmp         : out std_logic;
      rdFlush       : in  std_logic
   );
end entity SampleBuffer;

architecture BRAM of SampleBuffer is

   attribute ASYNC_REG      : string;
   attribute KEEP           : string;
   attribute SYN_PRESERVE   : boolean;

   constant MEM_DEPTH_C     : natural := ite( MEM_DEPTH_G = 0, 2**A_WIDTH_G, MEM_DEPTH_G );

   constant NUM_ADDR_BITS_C : natural := numBits(MEM_DEPTH_C - 1);

   subtype  RamAddr         is unsigned(NUM_ADDR_BITS_C - 1 downto 0);
   subtype  RamWord         is std_logic_vector(D_WIDTH_G - 1 downto 0);
   type     RamArray        is array(MEM_DEPTH_C - 1 downto 0) of RamWord;

   constant END_ADDR_C      : RamAddr := to_unsigned( MEM_DEPTH_C - 1, RamAddr'length );

   type     WrStateType     is (WRITE, NSMPLS_HI, HALT);

   type WrRegType   is record
      state       : WrStateType;
      nsmpl       : RamAddr;
      tgl         : std_logic;
   end record WrRegType;

   constant WR_REG_INIT_C   : WrRegType := (
      state       => WRITE,
      nsmpl       => (others => '0'),
      tgl         => '0'
   );

   type     RdStateType     is (WAITRD, PRELD, READ);

   type RdRegType   is record
      state       : RdStateType;
      raddr       : RamAddr;
      rdata       : RamWord;
      tgl         : std_logic;
   end record RdRegType;

   constant RD_REG_INIT_C   : RdRegType := (
      state       => WAITRD,
      raddr       => (others => '0'),
      rdata       => (others => 'X'),
      tgl         => '0'
   );

   signal rWr               : WrRegType := WR_REG_INIT_C;
   signal rWrIn             : WrRegType;

   signal waddr             : RamAddr := (others => '0');

   -- while the write is halted the reader may access
   -- these signals (-> false path)
   signal waddrCC           : RamAddr := (others => '0');
   attribute KEEP           of waddrCC : signal is "TRUE";
   attribute SYN_PRESERVE   of waddrCC : signal is true;
   signal nsmplCC           : RamAddr := (others => '0');
   attribute KEEP           of nsmplCC : signal is "TRUE";
   attribute SYN_PRESERVE   of nsmplCC : signal is true;

   signal DPRAMD            : RamArray;
   signal rdata             : RamWord;

   signal memWriteEna       : std_logic;
   signal memReadEna        : std_logic;

   signal rRd               : RdRegType := RD_REG_INIT_C;
   signal rRdIn             : RdRegType;
   signal rdLst             : std_logic;

   signal wrTglSync         : std_logic;
   signal rdTglSync         : std_logic;

begin

   U_WR_SYNC : entity work.SynchronizerBit
      port map (
         clk       => wrClk,
         rst       => '0',
         datInp(0) => rRd.tgl, 
         datOut(0) => rdTglSync
      );

   U_RD_SYNC : entity work.SynchronizerBit
      port map (
         clk       => rdClk,
         rst       => '0',
         datInp(0) => rWr.tgl, 
         datOut(0) => wrTglSync
      );

   P_COMB_WR : process ( rWr, wrEna, wrDat, rdTglSync ) is
      variable v : WrRegType;
   begin
      v           := rWr;

      memWriteEna <= '0';

      case ( rWr.state ) is
         when WRITE =>
            if ( ( wrDat(wrDat'left) and wrEna ) = '1' ) then
               if ( RamAddr'length >= 16 ) then
                  v.nsmpl (15 downto 0) := unsigned( wrDat(15 downto 0) );
               else
                  v.nsmpl               := unsigned( wrDat( v.nsmpl'range ) );
               end if;
               v.state     := NSMPLS_HI;
            else
               memWriteEna <= wrEna;
            end if;

         when NSMPLS_HI =>
            if ( wrEna = '1' ) then
               if ( RamAddr'length > 16 ) then
                  v.nsmpl(v.nsmpl'left downto 16) := unsigned( wrDat( RamAddr'length - 16 - 1 downto 0 ) );
               end if;
               v.tgl       := not rWr.tgl;
               v.state     := HALT;
            end if;

         when HALT =>
            if ( rdTglSync = rWr.tgl ) then
               v.state := WRITE;
            end if;
      end case;

      rWrIn <= v;
   end process P_COMB_WR;

   rdata   <= DPRAMD( to_integer( rRd.raddr ) );

   nsmplCC <= rWr.nsmpl;
   waddrCC <= waddr;

   P_COMB_RD : process ( rRd, rdEna, nsmplCC, waddrCC, wrTglSync, rdata, rdFlush ) is
      variable v : RdRegType;
   begin
      v        := rRd;
      rdEmp    <= '1';
      rdLst    <= '0';

      case ( rRd.state ) is
         when WAITRD =>
            if ( wrTglSync /= rRd.tgl ) then
               v.raddr := waddrCC - nsmplCC;
               -- simulator complains that MEM_DEPTH_C overflows
               -- which is the case if it's a power of two. This
               -- doesn't matter but we want to avoid the warning.
               if ( 2**NUM_ADDR_BITS_C > MEM_DEPTH_C ) then
                  if    ( nsmplCC >= MEM_DEPTH_C ) then
                     v.raddr := waddrCC;
                  elsif ( nsmplCC > waddrCC ) then 
                     v.raddr := waddrCC - nsmplCC + MEM_DEPTH_C;
                  end if;
               end if;
               v.state := PRELD;
            end if;

         when PRELD =>
            if ( rRd.raddr = END_ADDR_C ) then
               v.raddr := (others => '0');
            else
               v.raddr := rRd.raddr + 1;
            end if;
            v.rdata := rdata;
            v.state := READ;

         when READ  =>
            rdEmp   <= '0';
            if ( (rdFlush or rdEna) = '1' ) then
               if ( (rRd.raddr = waddrCC) or (rdFlush = '1') ) then
                  -- done (doesn't matter if we read the next item)
                  v.state := WAITRD;
                  v.tgl   := not rRd.tgl;
                  rdLst   <= '1';
               end if;
               if ( rRd.raddr = END_ADDR_C ) then
                  v.raddr := (others => '0');
               else
                  v.raddr := rRd.raddr + 1;
               end if;
               v.rdata := rdata;
            end if;

      end case;

      rRdIn <= v;
   end process P_COMB_RD;

   P_SEQ_WR : process ( wrClk ) is
   begin
      if ( rising_edge( wrClk ) ) then
         rWr <= rWrIn;
      end if;
   end process P_SEQ_WR;

   P_WR_RAM : process ( wrClk ) is
   begin
      if ( rising_edge( wrClk ) ) then
         if ( memWriteEna = '1' ) then
            DPRAMD( to_integer( waddr ) ) <= wrDat( D_WIDTH_G - 1 downto 0 );
            if ( waddr = END_ADDR_C ) then
               waddr                      <= (others => '0');
            else
               waddr                      <= waddr + 1;
            end if;
         end if;
      end if;
   end process P_WR_RAM;

   P_SEQ_RD : process ( rdClk ) is
   begin
      if ( rising_edge( rdClk ) ) then
         rRd <= rRdIn;
      end if;
   end process P_SEQ_RD;

   rdDat <= rdLst & rRd.rdata;

end architecture BRAM;
