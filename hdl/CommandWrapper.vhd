library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

use work.CommandMuxPkg.all;
use work.GitVersionPkg.all;

entity CommandWrapper is
   generic (
      I2C_SCL_G    : integer := -1; -- index of I2C SCL (to handle clock stretching)
      BBO_INIT_G   : std_logic_vector(7 downto 0) := x"FF";
      I2C_FREQ_G   : real    := 100.0E3;
      FIFO_FREQ_G  : real;
      ADC_FREQ_G   : real    := 130.0E6;
      MEM_DEPTH_G  : natural := 1024;
      COMMA_G      : std_logic_vector(7 downto 0) := x"CA";
      ESCAP_G      : std_logic_vector(7 downto 0) := x"55"
   );
   port (
      clk          : in  std_logic;
      rst          : in  std_logic;

      datIb        : in  std_logic_vector(7 downto 0);
      vldIb        : in  std_logic;
      rdyIb        : out std_logic;

      datOb        : out std_logic_vector(7 downto 0);
      vldOb        : out std_logic;
      rdyOb        : in  std_logic;

      bbo          : out std_logic_vector(7 downto 0);
      bbi          : in  std_logic_vector(7 downto 0);
      subCmdBB     : out SubCommandBBType;

      adcClk       : in  std_logic;
      adcRst       : in  std_logic := '0';

      -- bit 0 is the DOR (overrange) bit
      adcDataDDR   : in  std_logic_vector(8 downto 0);

      smplClk      : out std_logic;

      adcDcmLocked : out std_logic
   );
end entity CommandWrapper;

architecture rtl of CommandWrapper is

   constant NUM_CMDS_C        : natural := 3;

   constant CMD_VER_IDX_C     : natural := 0;
   constant CMD_BB_IDX_C      : natural := 1;
   constant CMD_ADC_MEM_IDX_C : natural := 2;

   signal   bussesIb          : SimpleBusMstArray(NUM_CMDS_C - 1 downto 0) := (others => SIMPLE_BUS_MST_INIT_C);
   signal   readysIb          : std_logic_vector (NUM_CMDS_C - 1 downto 0) := (others => '1'                  );
   signal   bussesOb          : SimpleBusMstArray(NUM_CMDS_C - 1 downto 0) := (others => SIMPLE_BUS_MST_INIT_C);
   signal   readysOb          : std_logic_vector (NUM_CMDS_C - 1 downto 0) := (others => '1'                  );

   signal   unstuffedBusIb    : SimpleBusMstType                           := SIMPLE_BUS_MST_INIT_C;
   signal   unstuffedRdyIb    : std_logic                                  := '1';
   signal   unstuffedBusOb    : SimpleBusMstType                           := SIMPLE_BUS_MST_INIT_C;
   signal   unstuffedRdyOb    : std_logic                                  := '1';

   signal   deStufferSynced   : std_logic;
   signal   deStufferAbort    : std_logic;

   constant VERSION_C : Slv8Array := (
      0 => GIT_VERSION_C(31 downto 24),
      1 => GIT_VERSION_C(23 downto 16),
      2 => GIT_VERSION_C(15 downto  8),
      3 => GIT_VERSION_C( 7 downto  0)
   );

   signal verAddr     : integer range -1 to VERSION_C'high := -1;
   signal verAddrIn   : integer range -1 to VERSION_C'high;

begin


   U_DESTUFFER : entity work.ByteDeStuffer
      generic map (
         COMMA_G     => COMMA_G,
         ESCAP_G     => ESCAP_G
      )
      port map (
         clk         => clk,
         rst         => rst,

         datOut      => unstuffedBusIb.dat,
         vldOut      => unstuffedBusIb.vld,
         lstOut      => unstuffedBusIb.lst,
         rdyOut      => unstuffedRdyIb,
         rstOut      => deStufferAbort,
         synOut      => deStufferSynced,

         datInp      => datIb,
         vldInp      => vldIb,
         rdyInp      => rdyIb
      );

   U_STUFFER : entity work.ByteStuffer
      generic map (
         COMMA_G     => COMMA_G,
         ESCAP_G     => ESCAP_G
      )
      port map (
         clk         => clk,
         rst         => rst,

         datInp      => unstuffedBusOb.dat,
         vldInp      => unstuffedBusOb.vld,
         lstInp      => unstuffedBusOb.lst,
         rdyInp      => unstuffedRdyOb,

         datOut      => datOb,
         vldOut      => vldOb,
         rdyOut      => rdyOb
      );


   U_MUXER : entity work.CommandMux
      generic map (
         NUM_CMDS_G   => NUM_CMDS_C
      )
      port map (
         clk          => clk,
         rst          => rst,

         busIb        => unstuffedBusIb,
         rdyIb        => unstuffedRdyIb,

         busOb        => unstuffedBusOb,
         rdyOb        => unstuffedRdyOb,

         busMuxedIb   => bussesIb,
         rdyMuxedIb   => readysIb,

         busMuxedOb   => bussesOb,
         rdyMuxedOb   => readysOb
      );

   -- Version not implemented yet; just echo
   P_VERSION_COMB : process ( bussesIb(CMD_VER_IDX_C), readysOb(CMD_VER_IDX_C), verAddr ) is
      variable v : SimpleBusMstType;
   begin
      v         := bussesIb(CMD_VER_IDX_C);
      v.lst     := '0';
      verAddrIn <= verAddr;
      if ( verAddr < 0 ) then
         readysIb(CMD_VER_IDX_C) <= readysOb(CMD_VER_IDX_C);
         bussesOb(CMD_VER_IDX_C) <= v;
      else
         readysIb(CMD_VER_IDX_C)        <= '1';
         bussesOb(CMD_VER_IDX_C).dat    <= VERSION_C( verAddr );
         bussesOb(CMD_VER_IDX_C).vld    <= '1';
         if ( verAddr = VERSION_C'high ) then
            bussesOb(CMD_VER_IDX_C).lst <= '1';
         else
            bussesOb(CMD_VER_IDX_C).lst <= '0';
         end if;
      end if;
      if ( verAddr < 0 ) then
         if ( ( v.vld and readysOb(CMD_VER_IDX_C) ) = '1' ) then
            verAddrIn <= 0;
         end if;
      else
         if ( readysOb(CMD_VER_IDX_C) = '1' ) then
            if ( verAddr = VERSION_C'high ) then
               verAddrIn <= -1;
            else
               verAddrIn <= verAddr + 1;
            end if;
         end if;
      end if;
   end process P_VERSION_COMB;

   P_VERSION_SEQ : process ( clk ) is
   begin
      if ( rising_edge( clk ) ) then
         if ( rst = '1' ) then
            verAddr <= -1;
         else
            verAddr <= verAddrIn;
         end if;
      end if;
   end process P_VERSION_SEQ;

    G_BITBANG : if ( NUM_CMDS_C > 1 ) generate

       U_BITBANG : entity work.CommandBitBang
          generic map (
             I2C_SCL_G    => I2C_SCL_G,
             BBO_INIT_G   => BBO_INIT_G,
             I2C_FREQ_G   => I2C_FREQ_G,
             CLOCK_FREQ_G => FIFO_FREQ_G
          )
          port map (
             clk          => clk,
             rst          => rst,

             mIb          => bussesIb(CMD_BB_IDX_C),
             rIb          => readysIb(CMD_BB_IDX_C),

             mOb          => bussesOb(CMD_BB_IDX_C),
             rOb          => readysOb(CMD_BB_IDX_C),

             bbi          => bbi,
             bbo          => bbo,
             subCmd       => subCmdBB
          );
    end generate G_BITBANG;

    G_ADC : if ( NUM_CMDS_C > 2 ) generate
       U_ADC_BUF : entity work.MaxAdc
          generic map (
             ADC_CLOCK_FREQ_G => ADC_FREQ_G,
             MEM_DEPTH_G      => MEM_DEPTH_G
          )
          port map (
             adcClk       => adcClk,
             adcRst       => adcRst,

             adcDataDDR   => adcDataDDR,

             smplClk      => smplClk,

             busClk       => clk,
             busRst       => rst,

             busIb        => bussesIb(CMD_ADC_MEM_IDX_C),
             rdyIb        => readysIb(CMD_ADC_MEM_IDX_C),

             busOb        => bussesOb(CMD_ADC_MEM_IDX_C),
             rdyOb        => readysOb(CMD_ADC_MEM_IDX_C),

             dcmLocked    => adcDcmLocked
          );
    end generate G_ADC;

end architecture rtl;
