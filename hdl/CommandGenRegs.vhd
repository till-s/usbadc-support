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
use ieee.math_real.all;

use work.BasicPkg.all;
use work.RegPkg.all;
use work.GenRegPkg.all;
use work.CommandMuxPkg.all;

-- common-use registers (leds, reconfiguration, ...)
--

entity CommandGenRegs is
   generic (
      ASYNC_G      : boolean := false
   );
   port (
      clk          : in  std_logic;
      rst          : in  std_logic;

      mIb          : in  SimpleBusMstType;
      rIb          : out std_logic;

      mOb          : out SimpleBusMstType;
      rOb          : in  std_logic;

      genRegOb     : out GenRegOutType;
      genRegIb     : in  GenRegInpType := GEN_REG_INP_INIT_C
   );
end entity CommandGenRegs;

architecture rtl of CommandGenRegs is

   type StateType is (INIT, RUN);

   function GEN_REG_OUT_INIT_F return GenRegOutType is
      variable v : GenRegOutType := GEN_REG_OUT_INIT_C;
   begin
      v.version    := GEN_REG_VERSION_1_C;
      return v;
   end function GEN_REG_OUT_INIT_F;
   
   type RegType is record
      state        : StateType;
      regRep       : RegisterRepType;
      genRegs      : GenRegOutType;
      reconfMagic  : std_logic_vector(7 downto 0);
   end record RegType;

   constant REG_INIT_C : RegType := (
      state        => INIT,
      regRep       => REGISTER_REP_INIT_C,
      genRegs      => GEN_REG_OUT_INIT_F,
      reconfMagic  => (others => '0')
   );

   signal r        : RegType := REG_INIT_C;
   signal rin      : RegType;

   signal regReq   : RegisterReqType;
   signal regRep   : RegisterRepType;

   signal mObLoc   : SimpleBusMstType;

begin

   mOb <= mObLoc;

   P_COMB : process ( r, regReq, genRegIb, rOb, mObLoc ) is
      variable v : RegType;
   begin
      v          := r;
      registerPrepareRegistered( regReq, v.regRep );

      -- version
      registerROBitsAt( 0, regReq, v.regRep, r.genRegs.version );
      -- scratch
      registerRWBitsAt( 1, regReq, v.regRep, v.genRegs.scratch );
      -- LED
      registerRWAt    ( 2, regReq, v.regRep, v.genRegs.leds, genRegIb.ledsSupported );
      -- reconfiguration features
      registerROBitsAt( 3, regReq, v.regRep, genRegIb.reconfigurable, 0 );

      -- reconfiguration request
      registerRWBitsAt( 4, regReq, v.regRep, v.reconfMagic, 0, ro => (genRegIb.reconfigurable = '0'));
      for i in r.genRegs.dbg'low to r.genRegs.dbg'high loop
         registerRWBitsAt( 8 + i, regReq, v.regRep, v.genRegs.dbg(i) );
      end loop;
      for i in genRegIb.dbg'low to genRegIb.dbg'high loop
         registerROBitsAt(16 + i, regReq, v.regRep, genRegIb.dbg(i) );
      end loop;
      registerXactRegistered( regReq, v.regRep );

      if ( r.reconfMagic = GEN_REG_RECONFIG_C ) then
         v.genRegs.reconfigure := '1';
      end if;

      case ( r.state ) is
         when INIT =>
            v.state         := RUN;
            v.genRegs.leds  := ( genRegIb.ledsInitial and genRegIb.ledsSupported );

         when RUN =>
      end case;

      genRegOb             <= r.genRegs;
      regRep               <= r.regRep;
      rin                  <= v;
   end process P_COMB;

   P_SEQ : process ( clk ) is
   begin
      if ( rising_edge( clk ) ) then
         if ( rst = '1' ) then
            r              <= REG_INIT_C;
            r.genRegs.leds <= ( genRegIb.ledsInitial and genRegIb.ledsSupported );
         else
            r <= rin;
         end if;
      end if;
   end process P_SEQ;

   U_REG : entity work.CommandReg
      generic map (
         ADDR_W_G     => 8,
         DATA_W_G     => 8,
         ASYNC_G      => false
      )
      port map (
         clk          => clk,
         rst          => rst,

         mIb          => mIb,
         rIb          => rIb,

         mOb          => mObLoc,
         rOb          => rOb,

         regClk       => open,

         addr         => regReq.addr,
         wdat         => regReq.wdat,
         vld          => regReq.vld,
         rdnw         => regReq.rdnw,
         rdat         => regRep.rdat,
         rdy          => regRep.rdy,
         err          => regRep.err
      );

end architecture rtl;
