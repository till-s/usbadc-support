library ieee;
use     ieee.std_logic_1164.all;
use     ieee.numeric_std.all;

entity SimPty is
   port (
      clk    : in  std_logic;
      vldOb  : out std_logic;
      datOb  : out std_logic_vector(7 downto 0);
      rdyOb  : in  std_logic;

      vldIb  : in  std_logic;
      datIb  : in  std_logic_vector(7 downto 0);
      rdyIb  : out std_logic;

      abrt   : out std_logic;
      abrtDon: in  std_logic := '1'
   );
end entity SimPty;

architecture sim of SimPty is

   procedure readPtyPoll_C(
      variable vld : out integer;
      variable dat : out integer
   );

   procedure writePty_C(
      variable vld : out integer;
      constant dat : in  integer
   );

   procedure writePtyPoll_C(
      variable rdy : out integer
   );


   attribute foreign of readPtyPoll_C  : procedure is "VHPIDIRECT readPtyPoll_C";
   attribute foreign of writePty_C     : procedure is "VHPIDIRECT writePty_C";
   attribute foreign of writePtyPoll_C : procedure is "VHPIDIRECT writePtyPoll_C";

   procedure readPtyPoll_C(
      variable vld : out integer;
      variable dat : out integer
   ) is
   begin
     assert false report "should not be executed" severity failure;
   end procedure readPtyPoll_C;

   procedure writePty_C(
      variable vld : out integer;
      constant dat : in  integer
   ) is
   begin
     assert false report "should not be executed" severity failure;
   end procedure writePty_C;

   procedure writePtyPoll_C(
      variable rdy : out integer
   ) is
   begin
     assert false report "should not be executed" severity failure;
   end procedure writePtyPoll_C;


   signal rdDat : std_logic_vector(7 downto 0);
   signal rdVld : std_logic := '0';
   signal rdAb  : std_logic := '0';

   signal wrRdy : std_logic := '0';
   signal wrAb  : std_logic := '0';

begin

   P_RD : process ( clk ) is
      variable vld : std_logic;
      variable vi  : integer;
      variable di  : integer;
   begin
      if ( rising_edge( clk ) ) then
         vld := rdVld;
         if ( abrtDon = '1' ) then
            rdAb <= '0';
         end if;
         if ( ( vld and rdyOb ) = '1' ) then
            vld := '0';
         end if;
         if ( vld = '0' ) then
            readPtyPoll_C( vi, di );
            if ( vi > 0 ) then
report "Updating data "&integer'image(di);
               rdDat <= std_logic_vector( to_unsigned( di, 8 ) );
               vld := '1';
            elsif ( vi < 0 ) then
               rdAb <= '1';
            end if;
         end if;
         rdVld <= vld;
      end if;
   end process P_RD;

   P_WR : process ( clk ) is
      variable rdy : std_logic;
      variable ri  : integer;
      variable st  : integer;
   begin
      if ( rising_edge( clk ) ) then
         if ( abrtDon = '1' ) then
            wrAb <= '0';
         end if;
         rdy := wrRdy;
         if ( ( rdy and vldIb ) = '1' ) then
            writePty_C( st, to_integer( unsigned( datIb ) ) );
			if ( st < 0 ) then
               wrAb <= '1';
            end if;
            rdy := '0';
         end if;
         if ( rdy = '0' ) then
            writePtyPoll_C( ri );
            if    ( ri > 0 ) then
               rdy := '1';
			elsif ( ri < 0 ) then
               wrAb <= '1';
            end if;
         end if;
         wrRdy <= rdy;
      end if;
   end process P_WR;


   vldOb <= rdVld;
   datOb <= rdDat;
   abrt  <= rdAb or wrAb;

   rdyIb <= wrRdy;
end architecture sim;
