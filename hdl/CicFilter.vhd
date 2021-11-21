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
      -- only used if in master mode
      decmInp      : in  unsigned(LD_MAX_DCM_G - 1 downto 0)          := (others => '0');
      -- decimator control signals to run another subordinate
      -- filter in parallel 
      cenbOut      : out std_logic_vector(NUM_STAGES_G - 1 downto -1);
      -- used by subordinate filter
      cenbInp      : in  std_logic_vector(NUM_STAGES_G - 1 downto -1) := (others => '0');
      dataInp      : in  signed  (DATA_WIDTH_G - 1 downto 0);
      -- overrange in
      dovrInp      : in  std_logic                                    := '0';
      dataOut      : out signed  (DATA_WIDTh_G + LD_MAX_DCM_G*NUM_STAGES_G - 1 downto 0);
      -- overrange_out
      dovrOut      : out std_logic;
      strbOut      : out std_logic
   );
end entity CicFilter;

architecture rtl of CicFilter is
   constant MAXW_C    :  natural := DATA_WIDTH_G + LD_MAX_DCM_G*NUM_STAGES_G;
   type     DataArray is array (natural range <>) of signed(MAXW_C - 1 downto 0);

   signal   dataIntg  : DataArray(0 to NUM_STAGES_G)                 := (others => (others => '0') );

   signal   dataDely  : DataArray(NUM_STAGES_G - 1 downto 0)         := (others => (others => '0') );
   signal   diffDecm  : DataArray(NUM_STAGES_G     downto 0)         := (others => (others => '0') );

   signal   cenDecm   : std_logic_vector(NUM_STAGES_G - 1 downto -1) := (others => '0');

   signal   count     : unsigned(decmInp'range)                      := (others => '0');

   signal   ovr       : std_logic                                    := '0';
   signal   strbLoc   : std_logic;

begin

   dataIntg(0) <= resize( dataInp, dataIntg(0)'length );

   G_INTG : for i in 1 to NUM_STAGES_G generate
   begin
      P_INTG : process ( clk ) is
      begin
         if ( rising_edge( clk ) ) then
            dataIntg(i) <= dataIntg(i) + dataIntg(i - 1);
         end if;
      end process P_INTG;
   end generate G_INTG;

   diffDecm(NUM_STAGES_G) <= dataIntg(NUM_STAGES_G);

   G_DIFF : for i in NUM_STAGES_G - 1 downto 0 generate
      P_DIFF : process ( clk ) is
      begin
         if ( rising_edge( clk ) ) then
            if ( cenDecm(i) = '1' ) then
               diffDecm(i) <= diffDecm(i+1) - dataDely(i);
               dataDely(i) <= diffDecm(i+1);
            end if;
         end if;
      end process P_DIFF;
   end generate G_DIFF;

   GEN_DCM_MST : if ( DCM_MASTER_G ) generate
      P_DCM : process ( clk ) is
      begin
         if ( rising_edge( clk ) ) then
            cenDecm <= '0' & cenDecm(cenDecm'left downto cenDecm'right + 1);
            if ( count = 0 ) then
               count                 <= decmInp;
               cenDecm(cenDecm'left) <= '1';
               ovr                   <= dovrInp;
            else
               count                 <= count - 1;
               ovr                   <= ovr or dovrInp;
            end if;
         end if;
      end process P_DCM;
   end generate GEN_DCM_MST;

   GEN_DCM_SUB : if ( not DCM_MASTER_G ) generate
      cenDecm <= cenbInp;
   end generate GEN_DCM_SUB;

   strbLoc <= cenDecm(cenDecm'right);

   P_OVR : process ( clk ) is
   begin
      if ( rising_edge( clk ) ) then
         if ( strbLoc = '1' ) then
            ovr <= dovrInp;
         else
            ovr <= ovr or dovrInp;
         end if;
      end if;
   end process P_OVR;

   dataOut <= diffDecm(0);
   dovrOut <= ovr;
   strbOut <= strbLoc;
   cenbOut <= cenDecm;

end architecture rtl;
