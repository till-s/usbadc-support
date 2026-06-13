--LB-MIT
--
-- MIT License
--
-- Copyright (c) 2026 Till Straumann
--
-- Permission is hereby granted, free of charge, to any person obtaining a copy
-- of this software and associated documentation files (the "Software"), to deal
-- in the Software without restriction, including without limitation the rights
-- to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
-- copies of the Software, and to permit persons to whom the Software is
-- furnished to do so, subject to the following conditions:
--
-- The above copyright notice and this permission notice shall be included in all
-- copies or substantial portions of the Software.
--
-- THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
-- IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
-- FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
-- AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
-- LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
-- OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
-- SOFTWARE.
--
--LE-MIT

library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

use work.BasicPkg.all;
use work.CommandMuxPkg.all;
use work.RegPkg.all;
use work.GenRegPkg.all;

entity CommandWrapper is
   generic (
      -- frequency at which the spi interface is clocked
      SPI_CLK_FREQ_G           : real;
      SPI_FREQ_G               : real    := 10.0E6;
      -- time from CS assertion to first SPI clock (0 -> 1/2 SPI clock)
      SPI_CSLO_NS_G            : real    := 0.0;
      -- min time from CS is held deasserted (0 -> 1/2 SPI clock)
      SPI_CSHI_NS_G            : real    := 0.0;
      -- delay CS deassertion after last SPI clock negedge (0 -> no delay)
      SPI_CSHI_DELAY_NS_G      : real    := 0.0;
      COMMA_G                  : std_logic_vector( 7 downto 0) := x"CA";
      ESCAP_G                  : std_logic_vector( 7 downto 0) := x"55";
      GIT_VERSION_G            : std_logic_vector(31 downto 0) := x"0000_0000";
      -- list of supported external commands
      CMDS_SUPPORTED_G         : CmdsSupportedType := APP_CMDS_SUPPORTED_NONE_C
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

      abrt         : in  std_logic := '0';
      abrtDon      : out std_logic := '0';

      -- register interface
      -- generic
      genRegOb     : out GenRegOutType   := GEN_REG_OUT_INIT_C;
      genRegIb     : in  GenRegInpType   := GEN_REG_INP_INIT_C;

      -- board version
      boardVersion : in  std_logic_vector(7 downto 0) := x"00";

      spiSClk      : out std_logic;
      spiMOSI      : out std_logic;
      spiCSb       : out std_logic;
      spiMISO      : in  std_logic := '0';

      -- application-specific commands (if any)
      bussesIb     : out SimpleBusMstArray(CMDS_SUPPORTED_G'high downto CMDS_SUPPORTED_G'low) := (others => SIMPLE_BUS_MST_INIT_C);
      readysIb     : in  std_logic_vector (CMDS_SUPPORTED_G'high downto CMDS_SUPPORTED_G'low) := (others => '1'                  );
      bussesOb     : in  SimpleBusMstArray(CMDS_SUPPORTED_G'high downto CMDS_SUPPORTED_G'low) := (others => SIMPLE_BUS_MST_INIT_C);
      readysOb     : out std_logic_vector (CMDS_SUPPORTED_G'high downto CMDS_SUPPORTED_G'low) := (others => '1'                  )
   );
end entity CommandWrapper;

architecture rtl of CommandWrapper is

   constant CMD_VER_IDX_C     : natural := to_integer(unsigned(CMD_VERSION_C   ));
   constant CMD_SPI_IDX_C     : natural := to_integer(unsigned(CMD_SPI_C       ));
   constant CMD_GEN_REG_IDX_C : natural := to_integer(unsigned(CMD_GEN_REGS_C  ));

   constant CMDS_SUPPORTED_C  : CmdsSupportedType := CMDS_SUPPORTED_BASIC_C & CMDS_SUPPORTED_G;

   constant NUM_CMDS_C        : natural := CMDS_SUPPORTED_C'length;
   -- shorter names:
   constant EXT_CMD_L_C       : natural := CMDS_SUPPORTED_G'high;
   constant EXT_CMD_R_C       : natural := CMDS_SUPPORTED_G'low;

   signal   bussesIbLoc       : SimpleBusMstArray(NUM_CMDS_C - 1 downto 0) := (others => SIMPLE_BUS_MST_INIT_C);
   signal   readysIbLoc       : std_logic_vector (NUM_CMDS_C - 1 downto 0) := (others => '1'                  );
   signal   bussesObLoc       : SimpleBusMstArray(NUM_CMDS_C - 1 downto 0) := (others => SIMPLE_BUS_MST_INIT_C);
   signal   readysObLoc       : std_logic_vector (NUM_CMDS_C - 1 downto 0) := (others => '1'                  );

   signal   unstuffedBusIb    : SimpleBusMstType                           := SIMPLE_BUS_MST_INIT_C;
   signal   unstuffedRdyIb    : std_logic                                  := '1';
   signal   unstuffedBusOb    : SimpleBusMstType                           := SIMPLE_BUS_MST_INIT_C;
   signal   unstuffedRdyOb    : std_logic                                  := '1';

   signal   deStufferSynced   : std_logic;
   signal   deStufferAbort    : std_logic;

   signal   pipelinedBusIb    : SimpleBusMstType                           := SIMPLE_BUS_MST_INIT_C;
   signal   pipelinedRdyIb    : std_logic                                  := '1';
   signal   pipelinedBusOb    : SimpleBusMstType                           := SIMPLE_BUS_MST_INIT_C;
   signal   pipelinedRdyOb    : std_logic                                  := '1';

   signal   stuffRst          : std_logic;

begin

   stuffRst <= ( rst or abrt );

   U_DESTUFFER : entity work.ByteDeStuffer
      generic map (
         COMMA_G     => COMMA_G,
         ESCAP_G     => ESCAP_G
      )
      port map (
         clk         => clk,
         rst         => stuffRst,

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

   U_PIPE_IB : entity work.SimpleBusPipeStage
      port map (
         clk         => clk,
         rst         => rst,

         busIb       => unstuffedBusIb,
         rdyIb       => unstuffedRdyIb,

         busOb       => pipelinedBusIb,
         rdyOb       => pipelinedRdyIb
      );

   U_STUFFER : entity work.ByteStuffer
      generic map (
         COMMA_G     => COMMA_G,
         ESCAP_G     => ESCAP_G
      )
      port map (
         clk         => clk,
         rst         => stuffRst,

         datInp      => unstuffedBusOb.dat,
         vldInp      => unstuffedBusOb.vld,
         lstInp      => unstuffedBusOb.lst,
         rdyInp      => unstuffedRdyOb,

         datOut      => datOb,
         vldOut      => vldOb,
         rdyOut      => rdyOb
      );

   U_PIPE_OB : entity work.SimpleBusPipeStage
      port map (
         clk         => clk,
         rst         => rst,

         busIb       => pipelinedBusOb,
         rdyIb       => pipelinedRdyOb,

         busOb       => unstuffedBusOb,
         rdyOb       => unstuffedRdyOb
      );

   U_MUXER : entity work.CommandMux
      generic map (
         CMDS_SUPPORTED_G => CMDS_SUPPORTED_C
      )
      port map (
         clk          => clk,
         rst          => rst,

         busIb        => pipelinedBusIb,
         rdyIb        => pipelinedRdyIb,

         busOb        => pipelinedBusOb,
         rdyOb        => pipelinedRdyOb,

         abrt         => abrt,
         abrtDon      => abrtDon,

         busMuxedIb   => bussesIbLoc,
         rdyMuxedIb   => readysIbLoc,

         busMuxedOb   => bussesObLoc,
         rdyMuxedOb   => readysObLoc
      );

   U_VERSION : entity work.CommandVersion
      generic map (
         GIT_VERSION_G   => GIT_VERSION_G
      )
      port map (
         clk          => clk,
         rst          => rst,

         hwVersion    => boardVersion,

         mIb          => bussesIbLoc(CMD_VER_IDX_C),
         rIb          => readysIbLoc(CMD_VER_IDX_C),

         mOb          => bussesObLoc(CMD_VER_IDX_C),
         rOb          => readysObLoc(CMD_VER_IDX_C)
      );

   U_SPI : entity work.CommandSpi
      generic map (
         CLOCK_FREQ_G => SPI_CLK_FREQ_G,
         SPI_FREQ_G   => SPI_FREQ_G,
         CSLO_NS_G    => SPI_CSLO_NS_G,
         CSHI_NS_G    => SPI_CSHI_NS_G,
         CSDL_NS_G    => SPI_CSHI_DELAY_NS_G
      )
      port map (
         clk          => clk,
         rst          => rst,

         mIb          => bussesIbLoc(CMD_SPI_IDX_C),
         rIb          => readysIbLoc(CMD_SPI_IDX_C),

         mOb          => bussesObLoc(CMD_SPI_IDX_C),
         rOb          => readysObLoc(CMD_SPI_IDX_C),

         spiSClk      => spiSClk,
         spiMOSI      => spiMOSI,
         spiCSb       => spiCSb,
         spiMISO      => spiMISO
      );

   U_GEN_REG : entity work.CommandGenRegs
      port map (
         clk          => clk,
         rst          => rst,

         mIb          => bussesIbLoc(CMD_GEN_REG_IDX_C),
         rIb          => readysIbLoc(CMD_GEN_REG_IDX_C),

         mOb          => bussesObLoc(CMD_GEN_REG_IDX_C),
         rOb          => readysObLoc(CMD_GEN_REG_IDX_C),

         genRegOb     => genRegOb,
         genRegIb     => genRegIb
      );

   bussesIb   (EXT_CMD_L_C downto EXT_CMD_R_C) <= bussesIbLoc(EXT_CMD_L_C downto EXT_CMD_R_C);
   readysIbLoc(EXT_CMD_L_C downto EXT_CMD_R_C) <= readysIb   (EXT_CMD_L_C downto EXT_CMD_R_C);

   bussesObLoc(EXT_CMD_L_C downto EXT_CMD_R_C) <= bussesOb   (EXT_CMD_L_C downto EXT_CMD_R_C);
   readysOb   (EXT_CMD_L_C downto EXT_CMD_R_C) <= readysObLoc(EXT_CMD_L_C downto EXT_CMD_R_C);

end architecture rtl;
