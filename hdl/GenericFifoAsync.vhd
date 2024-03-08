library ieee;
use     ieee.std_logic_1164.all;
use     ieee.numeric_std.all;

entity GenericFifoAsync is
   generic (
      LD_DEPTH_G    : natural;
      D_WIDTH_G     : natural;
      -- pipeline registers to use for gray->binary conversion
      -- -1 : wrFil/rdFil computation disabled
      --  0 : combinatorial
      --  1 : one pipe stage after every step
      --  2 : one pipe stage after every other step
      -- Note that the filled indicator lags the real situation
      -- by the number of pipeline stages, i.e., it may be too
      -- optimistic by the amount of stages (it may always be too
      -- pessimistic due to lag when synchronizing the 'other' side)!
      WR_FIL_PIPE_G : integer := -1;
      RD_FIL_PIPE_G : integer := -1;
      WR_CC_STGS_G  : natural :=  2;
      RD_CC_STGS_G  : natural :=  2;
      OUT_REG_G     : boolean := false
   );
   port (
      -- write side
      wrClk         : in  std_logic;
      wrEna         : in  std_logic;
      wrDat         : in  std_logic_vector(D_WIDTH_G - 1 downto 0);
      wrFul         : out std_logic;
      wrRstOut      : out std_logic;
      -- empty flag from write side (might be too optimistic; fifo might
      -- become empty before the write-side 'sees' it)
      wrEmp         : out std_logic;
      wrFil         : out unsigned(LD_DEPTH_G downto 0) := (others => '0');
      -- read side
      rdClk         : in  std_logic;
      rdRst         : in  std_logic := '0';
      rdRstOut      : out std_logic;
      rdEna         : in  std_logic;
      rdDat         : out std_logic_vector(D_WIDTH_G - 1 downto 0);
      rdEmp         : out std_logic;
      -- full flag from read side (might be too optimistic; fifo might
      -- become full before the read-side 'sees' it)
      rdFul         : out std_logic;
      rdFil         : out unsigned(LD_DEPTH_G downto 0) := (others => '0')
   );
end entity GenericFifoAsync;

architecture rtl of GenericFifoAsync is

   attribute syn_ramstyle : string;

   function l2(constant x : positive)
      return natural
   is
      variable rv  : natural  := 1;
      variable cmp : positive := 2;
   begin
      while ( x >= cmp ) loop
         cmp := 2*cmp;
         rv  := rv + 1;
      end loop;
      return rv;
   end function l2;

   function bin2Gray(constant x : unsigned)
      return unsigned
   is
   begin
      return unsigned( x xor shift_right(x, 1) );
   end function bin2Gray;

   function toSl(constant x : boolean)
      return std_logic
   is
   begin
      if ( x ) then return '1'; else return '0'; end if;
   end function toSl;

   subtype  MemWord       is std_logic_vector(D_WIDTH_G - 1 downto 0);
   type     MemType       is array(natural range 0 to 2**LD_DEPTH_G - 1) of MemWord;

   subtype  IdxType       is unsigned(LD_DEPTH_G - 1 downto 0);
   type     IdxArray      is array (natural range <>) of IdxType;

   constant GRAY_INIT_C   : IdxType   := bin2Gray( to_unsigned( 0, IdxType'length) );
   constant PTR_INIT_C    : IdxType   := to_unsigned( 1, IdxType'length );
   constant G2B_STAGES_C  : natural   := l2( LD_DEPTH_G );

   signal   mem           : MemType;
   attribute syn_ramstyle of mem      : signal is "block_ram"; --"registers";

   signal   wrPtrGray     : IdxType   := GRAY_INIT_C;
   signal   rdPtrGray     : IdxType   := GRAY_INIT_C;
   -- read and write pointers are 1 count ahead of their
   -- Gray-Code representation
   signal   rdPtr         : IdxType   := PTR_INIT_C;
   signal   wrPtr         : IdxType   := PTR_INIT_C;
   signal   wrRst         : std_logic := '0';
   signal   wrRstRtn      : std_logic;

   signal   full          : std_logic := '0';
   signal   empty         : std_logic := '1';
   signal   memVld        : std_logic := '0';

   signal   rdDatLoc      : MemWord;
   signal   rdEnaLoc      : std_logic;

   signal   rdRstLoc      : std_logic := '0';
   signal   rdRstReq      : std_logic := '0';

begin

   B_WR : block is
      signal graySynced    :  IdxType := GRAY_INIT_C;
      signal graySyncedSlv :  std_logic_vector(IdxType'range);
   begin

      G_WR_FIL : if ( WR_FIL_PIPE_G >= 0 ) generate
         signal g2b : IdxArray(G2B_STAGES_C downto 0)         := (others => (others => '0'));
         -- keep 'ful' flag and 'wrPtr' synced to gray-encoded value in pipeline
         signal ful : std_logic_vector(G2B_STAGES_C downto 0) := (others => '0');
         signal ptr : IdxArray(G2B_STAGES_C downto 0)         := (others => (others => '0'));
      begin

         g2b(g2b'left) <= graySynced;
         ful(ful'left) <= full;
         ptr(ptr'left) <= wrPtr;

         G_STG : for stg in G2B_STAGES_C - 1 downto 0 generate
            signal s : IdxType;
         begin
            s <= g2b(stg + 1) xor shift_right( g2b(stg + 1), 2**stg );

            G_REG : if ( WR_FIL_PIPE_G > 0 and (stg mod WR_FIL_PIPE_G = 0 ) ) generate
               P_REG : process ( wrClk ) is
               begin
                  if ( rising_edge( wrClk ) ) then
                     if ( wrRst = '1' ) then
                        -- reset value is all zeros and this is the same for all stages
                        g2b(stg) <= GRAY_INIT_C;
                        ful(stg) <= '0';
                        ptr(stg) <= PTR_INIT_C;
                     else
                        g2b(stg) <= s;
                        ful(stg) <= ful(stg + 1);
                        ptr(stg) <= ptr(stg + 1);
                     end if;
                  end if;
               end process P_REG;
            end generate G_REG;

            G_NO_REG : if ( WR_FIL_PIPE_G = 0 or (stg mod WR_FIL_PIPE_G /= 0 ) ) generate
               g2b(stg) <= s;
               ful(stg) <= ful(stg + 1);
               ptr(stg) <= ptr(stg + 1);
            end generate G_NO_REG;
         end generate G_STG;

         P_FIL : process( ful, ptr, g2b ) is
         begin
            if ( ful(0) = '1' ) then
               wrFil             <= (others => '0');
               wrFil(wrFil'left) <= '1';
            else
               wrFil             <= resize( ptr(0) - 1 - g2b(0), wrFil'length );
            end if;
         end process P_FIL;
      end generate G_WR_FIL;

      U_SYNC_2_WR : entity work.SynchronizerBit
         generic map (
            STAGES_G       => WR_CC_STGS_G,
            WIDTH_G        => IdxType'length,
            RSTPOL_G       => '0' -- matches initial value of gray-encoded wrPtr
         )
         port map (
            clk            => wrClk,
            rst            => wrRst,
            datInp         => std_logic_vector( rdPtrGray ),
            datOut         => graySyncedSlv
         );
      graySynced <= IdxType( graySyncedSlv );

      U_SYNC_2_WR_RST : entity work.SynchronizerBit
         generic map (
            STAGES_G       => WR_CC_STGS_G,
            WIDTH_G        => 1,
            RSTPOL_G       => '0'
         )
         port map (
            clk            => wrClk,
            rst            => '0',
            datInp(0)      => rdRstLoc,
            datOut(0)      => wrRst
         );

      P_SEQ : process ( wrClk ) is
         variable nowFull : std_logic;
         variable nxtGray : IdxType;
      begin
         if ( rising_edge( wrClk ) ) then
            if ( wrRst = '1' ) then
               full          <= '0';
               wrPtrGray     <= GRAY_INIT_C;
               wrPtr         <= PTR_INIT_C;
            else
               nowFull       := full;
               nxtGray       := bin2Gray( wrPtr );
               if ( graySynced /= wrPtrGray ) then
                  -- read pointer has moved; not full
                  nowFull := '0';
               end if;
               -- cant test nowFull here because (as long as we want
               -- to present a registered flag) the user doesn't 'see'
               -- that the read pointer just  moved during 'this' cycle.
               if ( ( wrEna and not full ) = '1' ) then
                  -- next write pointer == read pointer -> full
                  if ( nxtGray = graySynced ) then
                      nowFull := '1';
                  end if;
                  wrPtrGray                  <= nxtGray;
                  wrPtr                      <= wrPtr + 1;
                  mem( to_integer( wrPtr ) ) <= wrDat;
               end if;
               full       <= nowFull;
            end if;
         end if;
      end process P_SEQ;

      wrEmp <= not full and toSl( graySynced = wrPtrGray );
   end block B_WR;

   B_RD : block is
      signal graySynced    :  IdxType := GRAY_INIT_C;
      signal graySyncedSlv :  std_logic_vector(IdxType'range);
   begin

      G_RD_FIL : if ( RD_FIL_PIPE_G >= 0 ) generate
         signal g2b : IdxArray(G2B_STAGES_C downto 0)         := (others => (others => '0'));
         -- keep 'emp' flag and 'rdPtr' synced to gray-encoded value in pipeline
         signal emp : std_logic_vector(G2B_STAGES_C downto 0) := (others => '1');
         signal ptr : IdxArray(G2B_STAGES_C downto 0)         := (others => PTR_INIT_C);
      begin

         g2b(g2b'left) <= graySynced;
         emp(emp'left) <= empty;
         ptr(ptr'left) <= rdPtr;

         G_STG : for stg in G2B_STAGES_C - 1 downto 0 generate
            signal s : IdxType;
         begin
            s <= g2b(stg + 1) xor shift_right( g2b(stg + 1), 2**stg );

            G_REG : if ( RD_FIL_PIPE_G > 0 and (stg mod RD_FIL_PIPE_G = 0 ) ) generate
               P_REG : process ( rdClk ) is
               begin
                  if ( rising_edge( rdClk ) ) then
                     if ( rdRstLoc = '1' ) then
                        -- reset value is all zeros and this is the same for all stages
                        g2b(stg) <= GRAY_INIT_C;
                        emp(stg) <= '1';
                        ptr(stg) <= PTR_INIT_C;
                     else
                        g2b(stg) <= s;
                        emp(stg) <= emp(stg + 1);
                        ptr(stg) <= ptr(stg + 1);
                     end if;
                  end if;
               end process P_REG;
            end generate G_REG;

            G_NO_REG : if ( RD_FIL_PIPE_G = 0 or (stg mod RD_FIL_PIPE_G /= 0 ) ) generate
               g2b(stg) <= s;
               emp(stg) <= emp(stg + 1);
               ptr(stg) <= ptr(stg + 1);
            end generate G_NO_REG;
         end generate G_STG;

         P_FIL : process( emp, ptr, g2b ) is
            variable v : unsigned(rdFil'range);
         begin
            --v := resize( g2b(0) - ptr(0) + 1, wrFil'length );
            v := resize( ptr(0), wrFil'length );
            if ( v = 0 ) then
               -- rdPtr = wrPtr and not empty -> full to the brim
               v(v'left) := not emp(0);
            end if;
            rdFil <= v;
         end process P_FIL;
      end generate G_RD_FIL;

      U_SYNC_2_RD : entity work.SynchronizerBit
         generic map (
            STAGES_G       => RD_CC_STGS_G,
            WIDTH_G        => IdxType'length,
            RSTPOL_G       => '0' -- matches initial value of gray-encoded rdPtr
         )
         port map (
            clk            => rdClk,
            rst            => rdRstLoc,
            datInp         => std_logic_vector(wrPtrGray),
            datOut         => graySyncedSlv
         );

      graySynced <= IdxType( graySyncedSlv );

      U_SYNC_2_RD_RST : entity work.SynchronizerBit
         generic map (
            STAGES_G       => RD_CC_STGS_G,
            WIDTH_G        => 1,
            RSTPOL_G       => '0'
         )
         port map (
            clk            => rdClk,
            rst            => '0',
            datInp(0)      => wrRst,
            datOut(0)      => wrRstRtn
         );

      P_RD_RST : process ( rdClk ) is
      begin
         if ( rising_edge( rdClk ) ) then
            if    ( wrRstRtn = '1' ) then
               rdRstLoc <= '0';
               -- if a new request is issued while we are waiting for
               -- the current request to travel around then register it
               if ( rdRst = '1' ) then
                  rdRstReq <= '1';
               end if;
            elsif ( (rdRstReq or rdRst)    = '1' ) then
               rdRstLoc <= '1';
               rdRstReq <= '0';
            end if;
         end if;
      end process P_RD_RST;

      P_SEQ : process ( rdClk ) is
         variable nowEmpty : std_logic;
         variable nxtGray  : IdxType;
         variable nowVld  : std_logic;
      begin
         if ( rising_edge( rdClk ) ) then
            if ( rdRstLoc = '1' ) then
               empty         <= '1';
               memVld        <= '0';
               rdPtrGray     <= GRAY_INIT_C;
               rdPtr         <= PTR_INIT_C;
            else
               nowEmpty      := empty;
               nowVld        := memVld;
               nxtGray       := bin2Gray( rdPtr );
               -- write pointer has moved; not empty
               if ( graySynced /= rdPtrGray ) then
                  nowEmpty := '0';
               end if;
               -- has the user taken the output value?
               if ( (rdEnaLoc and nowVld) = '1' ) then
                 -- output has been consumed
                 nowVld := '0';
               end if;
               if ( (not nowVld and not nowEmpty) = '1' ) then
                  -- can read the next value.

                  -- prepare empty flag
                  -- next read pointer == read pointer -> empty
                  if ( nxtGray = graySynced ) then
                      nowEmpty := '1';
                  end if;
                  rdPtrGray <= nxtGray;
                  rdPtr     <= rdPtr + 1;
                  rdDatLoc  <= mem( to_integer( rdPtr ) );
                  nowVld    := '1';
               end if;
               empty      <= nowEmpty;
               memVld     <= nowVld;
            end if;
         end if;
      end process P_SEQ;

      rdFul <= not empty and toSl( graySynced = rdPtrGray );

      G_OUT_REG : if ( OUT_REG_G ) generate
         signal outReg : MemWord   := (others => '0');
         signal outVld : std_logic := '0';
      begin

         P_OUT_REG : process ( rdClk ) is
         begin
            if ( rising_edge( rdClk ) ) then
               if ( rdRstLoc = '1' ) then
                  outVld <= '0';
               else
                  if ( ( rdEna and outVld ) = '1' ) then
                     outVld <= '0';
                  end if;
                  if ( ( rdEnaLoc and memVld ) = '1' ) then
                     outReg <= rdDatLoc;
                     outVld <= '1';
                  end if;
               end if;
            end if;
         end process P_OUT_REG;

         rdDat    <= outReg;
         rdEmp    <= not outVld;
         rdEnaLoc <= ( ( not outVld ) or rdEna );
      end generate G_OUT_REG;

      G_NO_OUT_REG : if ( not OUT_REG_G ) generate
         rdEmp    <= not memVld;
         rdDat    <= rdDatLoc;
         rdEnaLoc <= rdEna;
      end generate G_NO_OUT_REG;

   end block B_RD;

   wrFul    <= full;
   wrRstOut <= wrRst;
   rdRstOut <= (rdRstLoc or rdRstReq);
 
end architecture rtl;
