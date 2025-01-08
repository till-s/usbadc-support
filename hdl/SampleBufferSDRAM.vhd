library ieee;
use     ieee.std_logic_1164.all;
use     ieee.numeric_std.all;

use     work.SDRAMPkg.all;

-- Bandwidth Calculation
--
-- SDRAM bandwidth reduction due to refresh:
--    (T_preload + T_refresh) / (Refresh_period / N_rows)
--
--    (T_RP_G + T_RFC_G)/(T_REF_G / 2**(A_WIDTH_G)
--
-- Bandwidth reduction due to bank switching (addressing is organized
-- such that accessing different rows within the same bank never happens
-- if addresses are issued *sequentially*):
--   (T_RCD + 1 cycle) / (N_columns)
--
--   (T_RCD_T * CLK_FREQ_G + 1)/(2**C_WIDTH_G)

entity SampleBuffer is
   generic (
      -- SDRAM address width
      A_WIDTH_G     : natural := 12;
      MEM_DEPTH_G   : natural := 0; -- unused; compatibility with BRAM version
      D_WIDTH_G     : natural := 20 -- currently only 20 bits supported
   );
   port (
      -- write side
      wrClk         : in  std_logic;
      wrEna         : in  std_logic;
      -- msb is 'command' flag
      wrDat         : in  std_logic_vector(D_WIDTH_G     downto 0);
      wrFul         : out std_logic; -- diagnostic signal

      -- sdram interface
      sdramClk      : in  std_logic;
      sdramReq      : out SDRAMReqType;
      sdramRep      : in  SDRAMRepType;

      -- read side
      rdClk         : in  std_logic;
      rdEna         : in  std_logic;
      rdDat         : out std_logic_vector(D_WIDTH_G     downto 0);
      rdEmp         : out std_logic;
      rdFlush       : in  std_logic
   );
end entity SampleBuffer;

architecture SDRAM of SampleBuffer is

   -- TODO fix fifo depths

   -- The write fifo must be deep enough to buffer incoming data during SDRAM page switches
   -- and refresh cycles (the worst case has these back-to-back).
   constant  LD_WR_FIFO_DEPTH_C : natural := 5;
   -- The read fifo must be deep enough to buffer SDRAM read pipeline (from request -> read data valid)
   -- plus the 1 cycle to store the first of the readback values (which is when the fifo is not empty
   -- anymore and issuing SDRAM read cycles stops). All values still in flight must be buffered!
   -- A depth of 8 is probably OK.
   constant  LD_RD_FIFO_DEPTH_C : natural := 4;

   attribute ASYNC_REG   : string;

   type      StateType   is (WRITE, MOP, SET_RDPTR_LO, SET_RDPTR_HI, RDPTR_DIFF, RD_COUNT, READ, LST);
   subtype   RamPtrType  is unsigned(A_WIDTH_G - 1 downto 0);
   subtype   RamCntType  is signed(A_WIDTH_G downto 0);

   -- word-address + offset computation
   --   N*20 = M*16 + o  => o = N*20 mod 16 => N*(16 + 4) mod 16 = N mod 4
   subtype   RamOffType  is unsigned(1 downto 0);

   type RegType   is record
      state       : StateType;
      nxtState    : StateType;
      sdramReq    : std_logic;
      sdramRdnwr  : std_logic;
      sdramWDat   : std_logic_vector(15 downto 0);
      ramPtr      : RamPtrType;
      wrCnt       : RamCntType;
      wrOff       : RamOffType;
      rdOff       : RamOffType;
      wrReg       : std_logic_vector(15 downto 0);
      rdReg       : std_logic_vector(15 downto 0);
      rdPipeCnt   : signed(4 downto 0);
      preload     : boolean;
   end record RegType;

   constant REG_INIT_C : RegType := (
      state       => WRITE,
      nxtState    => WRITE,
      sdramReq    => '0',
      sdramRdnwr  => '0',
      sdramWDat   => (others => '0'),
      ramPtr      => (others => '0'),
      wrCnt       => (others => '0'),
      wrOff       => (others => '0'),
      rdOff       => (others => '0'),
      wrReg       => (others => '0'),
      rdReg       => (others => '0'),
      rdPipeCnt   => (others => '1'),
      preload     => false
   );

   -- function relies on 20-bit data width and 16-bit RAM width
   procedure  ptrDiff(
      variable rv   : out RamPtrType;
      variable roff : out RamOffType;
      constant a    : in  RamCntType;
      constant aoff : in  RamOffType;
      constant n    : in  RamPtrType)
   is
      constant L_C : natural := RamPtrType'length + RamOffType'length;
      variable v   : unsigned(L_C - 1 downto 0);
      variable au  : RamPtrType;
   begin
      au   := RamPtrType( a(RamPtrType'range) );
      -- we compute 20*nsamples / 16 = (16 * nsamples + 4*nsamples ) / 16
      --                             = nsamples + nsamples/4
      -- keeping track of the fractional part in the 'offset'.
      v    := (au & aoff) - shift_left( resize( n, L_C ), 2 ) - resize( n, L_C );
      roff := v( roff'range );
      rv   := v( v'left downto roff'length );
   end procedure ptrDiff;

   signal    r           :  RegType := REG_INIT_C;
   signal    rin         :  RegType;

   signal wrFifoODat     :  std_logic_vector(D_WIDTH_G downto 0);
   signal wrFifoEmpty    :  std_logic;
   signal wrFifoREn      :  std_logic;

   signal rdFifoIDat     :  std_logic_vector(D_WIDTH_G downto 0);
   signal rdFifoWEn      :  std_logic;

   signal rdEnable       :  std_logic;
   signal sdramFlush     :  std_logic;

begin

   U_WR_FIFO : entity work.GenericFifoAsync
      generic map (
         LD_DEPTH_G => LD_WR_FIFO_DEPTH_C,
         D_WIDTH_G  => wrFifoODat'length,
         OUT_REG_G  => true
      )
      port map (
         wrClk      => wrClk,
         wrEna      => wrEna,
         wrDat      => wrDat,
         wrFul      => wrFul,

         rdClk      => sdramClk,
         rdEna      => wrFifoREn,
         rdDat      => wrFifoODat,
         rdEmp      => wrFifoEmpty
      );

   -- use output register as a buffer;
   -- the 'wrEmp' indicator then works
   -- as an 'almost empty' flag with 1
   -- value being (potentially) held 
   -- in the output register.
   U_RD_FIFO : entity work.GenericFifoAsync
      generic map (
         LD_DEPTH_G => LD_RD_FIFO_DEPTH_C,
         D_WIDTH_G  => rdFifoIDat'length,
         OUT_REG_G  => true
      )
      port map (
         wrClk      => sdramClk,
         wrEna      => rdFifoWEn,
         wrDat      => rdFifoIDat,
         wrFul      => open, -- must never fill;
         wrEmp      => rdEnable,
         wrRstOut   => sdramFlush,

         rdClk      => rdClk,
         rdRst      => rdFlush,
         rdEna      => rdEna,
         rdDat      => rdDat,
         rdEmp      => rdEmp
      );

   P_COMB : process (
      r,
      sdramRep,
      wrFifoEmpty, wrFifoODat,
      rdEnable,
      sdramFlush
   ) is
      variable v     : RegType;
      variable rdDon : boolean;
   begin
      v          := r;

      wrFifoREn  <= '0';
      rdFifoWEn  <= '0';
      rdFifoIDat <= '0' & sdramRep.rdat & r.rdReg(3 downto 0);

      if ( ( r.sdramReq and sdramRep.ack ) = '1' ) then
         v.sdramReq := '0';
         v.ramPtr   := r.ramPtr + 1;
         v.wrCnt    := r.wrCnt  - 1;
         v.state    := r.nxtState;
      end if;

      -- rdDon should be v.ramPtr = r.wrPtr to reflect
      -- 'this' cycle. For timing reasons we compute
      -- 1 cycle late; 
      rdDon         := ( r.wrCnt < 0 );

      case ( r.state ) is

         when WRITE =>
            v.nxtState       := WRITE;
            v.sdramRdnwr     := '0';
            -- wait for last req. being stored; even if the next
            -- item out of the fifo is a command...
            if ( (v.sdramReq or wrFifoEmpty) = '0' ) then
               wrFifoRen <= '1';
               if ( wrFifoODat( wrFifoODat'left ) = '1' ) then
                  -- don't consume yet
                  wrFifoRen  <= '0';
                  v.state    := SET_RDPTR_LO;
               else
                  v.sdramReq := '1';
                  v.wrOff    := r.wrOff + 1;
                  if    ( r.wrOff = 0 ) then
                     v.sdramWDat           := wrFifoODat(15 downto  0); 
                     v.wrReg( 3 downto  0) := wrFifoODat(19 downto 16);
                  elsif ( r.wrOff = 1 ) then
                     v.sdramWDat           := wrFifoODat(11 downto  0) & r.wrReg( 3 downto 0); 
                     v.wrReg( 7 downto  0) := wrFifoODat(19 downto 12);
                  elsif ( r.wrOff = 2 ) then
                     v.sdramWDat           := wrFifoODat( 7 downto  0) & r.wrReg( 7 downto 0); 
                     v.wrReg(11 downto  0) := wrFifoODat(19 downto  8);
                  elsif ( r.wrOff = 3 ) then
                     v.sdramWDat           := wrFifoODat( 3 downto  0) & r.wrReg(11 downto 0); 
                     v.wrReg(15 downto  0) := wrFifoODat(19 downto  4);
                     v.state               := MOP;
                  end if;
               end if;
            end if;

         when MOP =>
            if ( v.sdramReq = '0' ) then
               v.sdramWDat := r.wrReg;
               v.sdramReq  := '1';
            end if;

         when SET_RDPTR_LO =>
            -- wrFifo is not empty at this point
            wrFifoRen             <= '1';
            -- save write pointer
            v.wrCnt                  := RamCntType( '0' & r.ramPtr );
            if ( v.ramPtr'length >= 16 ) then
               v.ramPtr(15 downto 0) := unsigned( wrFifoODat(15 downto 0) );
            else
               v.ramPtr              := unsigned( wrFifoODat( v.ramPtr'range ) );
            end if;
            v.state               := SET_RDPTR_HI;

         when SET_RDPTR_HI =>
            if ( wrFifoEmpty = '0' ) then
               wrFifoRen                         <= '1';
               if ( v.ramPtr'length > 16 ) then
                  v.ramPtr(v.ramPtr'left downto 16) := unsigned( wrFifoODat( v.ramPtr'length - 16 - 1 downto 0 ) );
               end if;
               v.state                           := RDPTR_DIFF;
            end if;

         when RDPTR_DIFF =>
            -- set ramPtr to D_WIDTH_G*nSamples / 16; with D_WIDTH_G = 20:
            --      20*nSamples = (16*nNSamples + 4*nSamples) / 16 => nSamples + nSamples / 4
            -- At this point, the write-pointer is stored in 'wrCnt' and the count
            -- in 'ramPtr'; a bit confusing but we'll get it right shortly...
            ptrDiff( v.ramPtr, v.rdOff, r.wrCnt, r.wrOff, r.ramPtr );
            v.state            := RD_COUNT;
            -- pre-decrement write count; we use the sign bit to determine if we are done
            v.wrCnt            := '0' & signed( RamPtrType(r.wrCnt(RamPtrType'range)) - 1 );

         when RD_COUNT =>
            -- wrCnt now holds end-address - 1; now subtract the start address
            v.wrCnt            := '0' & signed( RamPtrType(r.wrCnt(RamPtrType'range)) - r.ramPtr );
            v.state            := READ;
            v.preload          := true;
            v.rdPipeCnt        := (others => '1');

         when READ  =>
            v.nxtState         := READ;
            v.sdramRdnwr       := '1';
            -- issue next read
            if ( not rdDon and ( (not r.sdramReq and rdEnable) = '1' ) ) then
               v.sdramReq  := '1';
               v.rdPipeCnt := r.rdPipeCnt + 1;
            end if;
            

            -- next word out of ram
            if ( sdramRep.vld = '1' ) then

               if    ( r.rdOff = 0 ) then
                  v.rdReg(15 downto 0)  := sdramRep.rdat;
               elsif ( r.rdOff = 1 ) then
                  v.rdReg(11 downto 0)  := sdramRep.rdat(15 downto  4);
               elsif ( r.rdOff = 2 ) then
                  v.rdReg( 7 downto 0)  := sdramRep.rdat(15 downto  8);
               elsif ( r.rdOff = 3 ) then
                  v.rdReg( 3 downto 0)  := sdramRep.rdat(15 downto 12);
               end if;

               v.preload   := false;
               v.rdOff     := r.rdOff + 1;
               v.rdPipeCnt := v.rdPipeCnt - 1; -- use v on RHS; might have been incremented above

               if ( not r.preload ) then
                  rdFifoWEn <= '1';
                  if    ( r.rdOff = 1 ) then
                     rdFifoIDat            <= '0' & sdramRep.rdat( 3 downto  0) & r.rdReg(15 downto  0);
                  elsif ( r.rdOff = 2 ) then
                     rdFifoIDat            <= '0' & sdramRep.rdat( 7 downto  0) & r.rdReg(11 downto 0);
                  elsif ( r.rdOff = 3 ) then
                     rdFifoIDat            <= '0' & sdramRep.rdat(11 downto  0) & r.rdReg( 7 downto 0);
                  elsif ( r.rdOff = 0 ) then
                     rdFifoIDat            <= '0' & sdramRep.rdat(15 downto  0) & r.rdReg( 3 downto 0);
                     -- preload the next word (nothing to output then)
                     v.preload             := true;
                     v.rdOff               := r.rdOff;
                     -- if rdDon is true then the rdPipeCnt cannot have been incremented
                     -- it is thus enough to to check r.rdPipeCnt = 0
                     if ( rdDon and (r.rdPipeCnt = 0 ) ) then
                        rdFifoIDat( rdFifoIDat'left ) <= '1'; -- 'last' flag
                     end if;
                  end if;
               end if;
            end if;

            if ( rdDon and ( r.rdPipeCnt < 0 ) ) then
               if ( r.preload ) then
                  v.state := WRITE;
               else
                  v.state := LST;
               end if;
            end if;

            if ( sdramFlush = '1' ) then
               v.state    := WRITE;
               v.nxtState := WRITE;
            end if;

         when LST => -- last bits of incomplete word are still in 'wrReg'
            rdFifoWEn <= '1';
            if    ( r.rdOff = 1 ) then
               rdFifoIDat            <= '1' & r.wrReg( 3 downto  0) & r.rdReg(15 downto  0);
            elsif ( r.rdOff = 2 ) then
               rdFifoIDat            <= '1' & r.wrReg( 7 downto  0) & r.rdReg(11 downto 0);
            elsif ( r.rdOff = 3 ) then
               rdFifoIDat            <= '1' & r.wrReg(11 downto  0) & r.rdReg( 7 downto 0);
            elsif ( r.rdOff = 0 ) then
               rdFifoIDat            <= '1' & r.wrReg(15 downto  0) & r.rdReg( 3 downto 0);
            end if;
            v.state := WRITE;

      end case;

      rin      <= v;
   end process P_COMB;

   P_SEQ : process ( sdramClk ) is
   begin
      if rising_edge( sdramClk ) then
         r <= rin;
      end if;
   end process P_SEQ;

   sdramReq.req   <= r.sdramReq;
   sdramReq.rdnwr <= r.sdramRdnwr;
   sdramReq.wdat  <= r.sdramWDat;
   sdramReq.addr  <= std_logic_vector( resize( r.ramPtr, sdramReq.addr'length ) );
 
end architecture SDRAM;
