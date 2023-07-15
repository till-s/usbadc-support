library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;
use ieee.math_real.all;

entity CicFilter is
   generic (
      DATA_WIDTH_G : natural;
      LD_MAX_DCM_G : natural;
      NUM_STAGES_G : natural;
      DCM_MASTER_G : boolean := true
   );
   port (
      clk          : in  std_logic;
      rst          : in  std_logic;
      cen          : in  std_logic := '1';
      -- only used if in master mode
      decmInp      : in  unsigned(LD_MAX_DCM_G - 1 downto 0)          := (others => '0');
      -- decimator control signals to run another subordinate
      -- filter in parallel
      cenbOut      : out std_logic_vector(NUM_STAGES_G  downto 0);
      -- used by subordinate filter
      cenbInp      : in  std_logic_vector(NUM_STAGES_G  downto 0)     := (others => '0');
      dataInp      : in  signed  (DATA_WIDTH_G - 1 downto 0);
      -- overrange in
      dovrInp      : in  std_logic                                    := '0';
      -- trigger in; delayed with the data
      trigInp      : in  std_logic                                    := '0';
      -- aux signal; delayed
      dataOut      : out signed  (DATA_WIDTH_G + LD_MAX_DCM_G*NUM_STAGES_G - 1 downto 0);
      -- overrange_out
      dovrOut      : out std_logic;
      -- trigger out
      trigOut      : out std_logic;
      strbOut      : out std_logic
   );
end entity CicFilter;

architecture rtl of CicFilter is
   constant MAXW_C    :  natural := DATA_WIDTH_G + LD_MAX_DCM_G*NUM_STAGES_G;
   type     DataArray is array (natural range <>) of signed(MAXW_C - 1 downto 0);

   signal   dataIntg  : DataArray(0 to NUM_STAGES_G)                 := (others => (others => '0') );
   signal   dovrIDel  : std_logic_vector(0 to NUM_STAGES_G)          := (others => '0');
   signal   trigIDel  : std_logic_vector(0 to NUM_STAGES_G)          := (others => '0');

   signal   dataDely  : DataArray(NUM_STAGES_G - 1 downto 0)         := (others => (others => '0') );
   signal   diffDecm  : DataArray(NUM_STAGES_G     downto 0)         := (others => (others => '0') );
   signal   dovrDDel  : std_logic_vector(NUM_STAGES_G downto 0)      := (others => '0');
   signal   trigDDel  : std_logic_vector(NUM_STAGES_G downto 0)      := (others => '0');

   signal   cenDecm   : std_logic_vector(NUM_STAGES_G downto 0)      := (others => '0');

   signal   count     : unsigned(decmInp'range)                      := (others => '0');

   signal   dovrDecm  : std_logic                                    := '0';
   signal   trigDecm  : std_logic                                    := '0';
   signal   strbLoc   : std_logic;

begin

-- Pipeline
--
--  input dor dataIng0 dataIntg1 diffDecm1  diffDecm0 dorDely1  OvrSum    OvrDelayOut
--   s0    o0
--   s1    o1   s0
--   s2    o2   s1        s0                             o0      o-1|o-2
--   s3    o3   s2        s1        s0                           o0         o0|o-1|0-2
--   s4         s3        s2                  s0                 o0 | o1
--   s1         s4        s3
--

   dataIntg(0) <= resize( dataInp, dataIntg(0)'length );
   dovrIDel(0) <= dovrInp;
   trigIDel(0) <= trigInp;

   G_INTG : for i in 1 to NUM_STAGES_G generate
   begin
      P_INTG : process ( clk ) is
      begin
         if ( rising_edge( clk ) ) then
            if ( cen = '1' ) then
               dataIntg(i) <= dataIntg(i) + dataIntg(i - 1);
               dovrIDel(i) <= dovrIDel(i - 1);
               trigIDel(i) <= trigIDel(i - 1);
            end if;
         end if;
      end process P_INTG;
   end generate G_INTG;

   diffDecm(NUM_STAGES_G) <= dataIntg(NUM_STAGES_G);
   -- or together all overranges accumulated during a decimation cycle
   dovrDDel(NUM_STAGES_G) <= (dovrIDel(NUM_STAGES_G) or dovrDecm);
   -- or together all triggers accumulated during a decimation cycle
   trigDDel(NUM_STAGES_G) <= (trigIDel(NUM_STAGES_G) or trigDecm);

   G_DIFF : for i in NUM_STAGES_G - 1 downto 0 generate
      P_DIFF : process ( clk ) is
      begin
         if ( rising_edge( clk ) ) then
            if ( (cen and cenDecm(i+1)) = '1' ) then
               diffDecm(i) <= diffDecm(i+1) - dataDely(i);
               dataDely(i) <= diffDecm(i+1);
               dovrDDel(i) <= dovrDDel(i+1);
               trigDDel(i) <= trigDDel(i+1);
            end if;
         end if;
      end process P_DIFF;
   end generate G_DIFF;


   GEN_DCM_MST : if ( DCM_MASTER_G ) generate
      P_CEN : process ( clk ) is
      begin
         if ( rising_edge( clk ) ) then
            if ( cen = '1' ) then
               cenDecm <= '0' & cenDecm(cenDecm'left downto cenDecm'right + 1);
               if ( count = 0 ) then
                  count                 <= decmInp;
                  cenDecm(cenDecm'left) <= '1';
               else
                  count                 <= count - 1;
               end if;
            end if;
         end if;
      end process P_CEN;
   end generate GEN_DCM_MST;

   GEN_DCM_SUB : if ( not DCM_MASTER_G ) generate
      cenDecm <= cenbInp;
   end generate GEN_DCM_SUB;

   strbLoc <= cenDecm(cenDecm'right) and cen;

   P_OVR : process ( clk ) is
   begin
      if ( rising_edge( clk ) ) then
         if ( cen = '1' ) then
            if ( cenDecm(cenDecm'left) = '1' ) then
               dovrDecm <= '0';
               trigDecm <= '0';
            else
               dovrDecm <= dovrDDel(NUM_STAGES_G);
               trigDecm <= trigDDel(NUM_STAGES_G);
            end if;
         end if;
      end if;
   end process P_OVR;

   dataOut <= diffDecm(0);
   dovrOut <= dovrDDel(0);
   trigOut <= trigDDel(0);
   strbOut <= strbLoc;
   cenbOut <= cenDecm;

end architecture rtl;
