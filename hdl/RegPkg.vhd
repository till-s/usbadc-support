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

package RegPkg is

   type RegisterReqType is record
      addr      : unsigned(7 downto 0);
      vld       : std_logic;
      rdnw      : std_logic;
      wdat      : std_logic_vector(7 downto 0);
   end record RegisterReqType;

   constant REGISTER_REQ_INIT_C : RegisterReqType := (
      addr      => (others => '0'),
      vld       => '0',
      rdnw      => '1',
      wdat      => (others => '0')
   );

   type RegisterRepType is record
      rdy       : std_logic;
      rDat      : std_logic_vector(7 downto 0);
      err       : std_logic;
   end record RegisterRepType;

   constant REGISTER_REP_INIT_C : RegisterRepType := (
      rdy       => '0',
      rDat      => (others => '0'),
      err       => '1'
   );

   constant REGISTER_REP_FORCE_ERR_C : RegisterRepType := (
      rdy       => '1',
      rDat      => (others => '0'),
      err       => '1'
   );

   procedure registerPrepareCombinatorial(signal m : in RegisterReqType; variable s : inout RegisterRepType);
   procedure registerPrepareRegistered(signal m : in RegisterReqType; variable s : inout RegisterRepType);

   procedure registerRWAt(
      constant adr : in    natural;
      signal   mst : in    RegisterReqType;
      variable sub : inout RegisterRepType;
      variable reg : inout std_logic_vector(7 downto 0);
      constant msk : in    std_logic_vector(7 downto 0) := x"FF";
      constant ro  : in    boolean := false
   );

   procedure registerRWBitsAt(
      constant adr : in    natural;
      signal   mst : in    RegisterReqType;
      variable sub : inout RegisterRepType;
      variable reg : inout std_logic_vector;
      constant off : in    natural range 0 to 7 := 0;
      constant ro  : in    boolean := false
   );

   procedure registerROBitsAt(
      constant adr : in    natural;
      signal   mst : in    RegisterReqType;
      variable sub : inout RegisterRepType;
      constant reg : in    std_logic_vector;
      constant off : in    natural range 0 to 7 := 0
   );

   procedure registerRWBitsAt(
      constant adr : in    natural;
      signal   mst : in    RegisterReqType;
      variable sub : inout RegisterRepType;
      variable reg : inout std_logic;
      constant off : in    natural range 0 to 7 := 0;
      constant ro  : in    boolean := false
   );

   procedure registerROBitsAt(
      constant adr : in    natural;
      signal   mst : in    RegisterReqType;
      variable sub : inout RegisterRepType;
      constant reg : in    std_logic;
      constant off : in    natural range 0 to 7 := 0
   );


   procedure registerXactRegistered(
      signal   m : in    RegisterReqType;
      variable s : inout RegisterRepType
   );

   procedure registerXactCombinatorial(
      signal   m : in    RegisterReqType;
      signal   o : out   RegisterRepType;
      variable s : in    RegisterRepType
   );

   function  registerOnAccess(
      constant adr : in    natural;
      signal   mst : in    RegisterReqType;
      constant sub : in    RegisterRepType
   ) return boolean;

   function  registerOnRead(
      constant adr : in    natural;
      signal   mst : in    RegisterReqType;
      constant sub : in    RegisterRepType
   ) return boolean;

   function  registerOnWrite(
      constant adr : in    natural;
      signal   mst : in    RegisterReqType;
      constant sub : in    RegisterRepType
   ) return boolean;

end package RegPkg;

package body RegPkg is

   procedure registerPrepareCombinatorial(signal m : in RegisterReqType; variable s : inout RegisterRepType) is
   begin
      s     := REGISTER_REP_INIT_C;
      s.rdy := '1';
      s.err := '1';
   end procedure registerPrepareCombinatorial;

   procedure registerPrepareRegistered(signal m : in RegisterReqType; variable s : inout RegisterRepType) is
   begin
      s.rdat := (others => '0');
      s.err  := '1';
   end procedure registerPrepareRegistered;


   function slice(
      constant v : in std_logic_vector(7 downto 0);
      constant x : in std_logic_vector
   ) return std_logic_vector is
   begin
      if ( x'ascending ) then
         return v(v'left - x'left downto v'left - x'right);
      else
         return v(x'range);
      end if;
   end function slice;

   procedure fill(
      variable v : inout std_logic_vector(7 downto 0);
      constant x : in    std_logic_vector
   ) is
   begin
      if ( x'ascending ) then
         v(v'left - x'left downto v'left - x'right) := x;
      else
         v(x'range) := x;
      end if;
   end procedure fill;

   function widen(
      constant x : in std_logic_vector
   ) return std_logic_vector is
      variable v : std_logic_vector(7 downto 0);
   begin
      v := (others => '0');
      fill(v, x);
      return v;
   end function widen;
 
   procedure registerRWAt(
      constant adr : in    natural;
      signal   mst : in    RegisterReqType;
      variable sub : inout RegisterRepType;
      variable reg : inout std_logic_vector(7 downto 0);
      constant msk : in    std_logic_vector(7 downto 0) := x"FF";
      constant ro  : in    boolean := false
   ) is
   begin
      if (mst.vld = '1' and adr = to_integer(mst.addr)) then
         sub.err := '0';
         if ( mst.rdnw = '1' ) then
            sub.rdat := (reg and msk) or (sub.rdat and not msk);
         elsif (sub.rdy = '1' and not ro) then
            reg := (mst.wdat and msk) or (reg and not msk);
         end if;
      end if;
   end registerRWAt;

   procedure registerRWBitsAt(
      constant adr : in    natural;
      signal   mst : in    RegisterReqType;
      variable sub : inout RegisterRepType;
      variable reg : inout std_logic_vector;
      constant off : in    natural range 0 to 7 := 0;
      constant ro  : in    boolean := false
   ) is
      constant L   : natural := reg'length;
      variable msk : std_logic_vector(7 downto 0) := (others => '0');
      variable val : std_logic_vector(7 downto 0);
   begin
      msk(off + L - 1 downto off) := (others => '1');
      val := std_logic_vector(shift_left(resize(unsigned(reg), val'length), off));
      registerRWAt(adr, mst, sub, val, msk, ro);
      reg := std_logic_vector(resize(shift_right(unsigned(val), off), reg'length));
   end procedure registerRWBitsAt;

   procedure registerRWBitsAt(
      constant adr : in    natural;
      signal   mst : in    RegisterReqType;
      variable sub : inout RegisterRepType;
      variable reg : inout std_logic;
      constant off : in    natural range 0 to 7 := 0;
      constant ro  : in    boolean := false
   ) is
      variable msk : std_logic_vector(7 downto 0) := (others => '0');
      variable val : std_logic_vector(7 downto 0) := (others => '0');
   begin
      msk(off) := '1';
      val(off) := reg;
      registerRWAt(adr, mst, sub, val, msk, ro);
      reg      := val(off);
   end procedure registerRWBitsAt;

   procedure registerROBitsAt(
      constant adr : in    natural;
      signal   mst : in    RegisterReqType;
      variable sub : inout RegisterRepType;
      constant reg : in    std_logic;
      constant off : in    natural range 0 to 7 := 0
   ) is
      variable val : std_logic := reg;
   begin
      registerRWBitsAt(adr, mst, sub, val, off, true);
   end procedure registerROBitsAt;

   procedure registerROBitsAt(
      constant adr : in    natural;
      signal   mst : in    RegisterReqType;
      variable sub : inout RegisterRepType;
      constant reg : in    std_logic_vector;
      constant off : in    natural range 0 to 7 := 0
   ) is
      variable val : std_logic_vector(reg'range) := reg;
   begin
      registerRWBitsAt(adr, mst, sub, val, off, true);
   end procedure registerROBitsAt;


   procedure registerXactRegistered(
      signal   m : in    RegisterReqType;
      variable s : inout RegisterRepType
   ) is
   begin
      if ( m.vld = '1' ) then
         if ( s.rdy = '0' ) then
            -- data are available in rdat or consumed from wdat in next cycle
            s.rdy := '1';
         else
            -- data were consumed; rdat is not ready
            s.rdy := '0';
         end if;
      end if;
   end procedure registerXactRegistered;

   procedure registerXactCombinatorial(
      signal   m : in    RegisterReqType;
      signal   o : out   RegisterRepType;
      variable s : in    RegisterRepType
   ) is
   begin
      o <= s;
   end procedure registerXactCombinatorial;

   function  registerOnAccess(
      constant adr : in    natural;
      signal   mst : in    RegisterReqType;
      constant sub : in    RegisterRepType
   ) return boolean is
   begin
      return (mst.vld = '1' and adr = to_integer(mst.addr) and sub.rdy = '1');
   end function  registerOnAccess;

   function  registerOnRead(
      constant adr : in    natural;
      signal   mst : in    RegisterReqType;
      constant sub : in    RegisterRepType
   ) return boolean is
   begin
      return registerOnAccess(adr, mst, sub) and (mst.rdnw = '1');
   end function registerOnRead;

   function  registerOnWrite(
      constant adr : in    natural;
      signal   mst : in    RegisterReqType;
      constant sub : in    RegisterRepType
   ) return boolean is
   begin
      return registerOnAccess(adr, mst, sub) and (mst.rdnw = '0');
   end function registerOnWrite;

end package body RegPkg;
