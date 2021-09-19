library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity SFIFO is
   port (
      clk   : in  std_logic;
      rst   : in  std_logic;

      datIb : in  std_logic_vector(7 downto 0);
      vldIb : in  std_logic;
      rdyIb : out std_logic;
      
      datOb : out std_logic_vector(7 downto 0);
      vldOb : out std_logic;
      rdyOb : in  std_logic
   );
end entity SFIFO;

architecture rtl of SFIFO is

   type FifoArray is array (1 downto 0) of std_logic_vector(7 downto 0);

   type RegType is record 
      fifo  : FifoArray;
      rp    : unsigned(0 downto 0);
      wp    : unsigned(0 downto 0);
      fill  : natural range 0 to 2;
   end record RegType;


   constant REG_INIT_C : RegType := (
      fifo  => (others => (others => '0')),
      rp    => (others => '0'),
      wp    => (others => '0'),
      fill  => 0
   );

   signal r     : RegType := REG_INIT_C;
   signal rin   : RegType;

begin

   P_COMB : process (r, datIb, vldIb, rdyOb) is
      variable v : RegType;
   begin
      v   := r;

      if ( r.fill < 2 and vldIb = '1' ) then
         v.fifo(to_integer(r.wp)) := datIb;
         v.fill := r.fill + 1;
         v.wp   := r.wp   + 1;
      end if;

      if ( r.fill > 0 and rdyOb = '1' ) then
         v.fill := v.fill - 1;
         v.rp   := r.rp   + 1; 
      end if;
         
      rin <= v;

      if ( r.fill < 2 ) then rdyIb <= '1'; else rdyIb <= '0'; end if;
      if ( r.fill > 0 ) then vldOb <= '1'; else vldOb <= '0'; end if;
   end process P_COMB;

   P_SEQ : process (clk) is
   begin
      if ( rising_edge( clk ) ) then
         if ( rst = '1' ) then
            r <= REG_INIT_C;
         else
            r <= rin;
         end if;
      end if;
   end process P_SEQ;

   datOb <= r.fifo(to_integer(r.rp));

end architecture rtl;
