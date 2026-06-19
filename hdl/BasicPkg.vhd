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
use     ieee.std_logic_1164.all;
use     ieee.math_real.all;

package BasicPkg is

   function numBits(constant x : in integer) return integer;

   function toSl(constant x : in boolean) return std_logic;

   function ite(constant x : in boolean; constant a,b: in std_logic) return std_logic;
   function ite(constant x : in boolean; constant a,b: in integer  ) return integer;

   subtype Slv8Type  is std_logic_vector(7 downto 0);
   type    Slv8Array is array(integer range <>) of Slv8Type;

   constant SLV8_ARRAY_EMPTY_C : Slv8Array(0 downto 1) := (others => (others =>'0'));

   function toSlv(constant a : in Slv8Array)
   return std_logic_vector;

   type NaturalArray is array(integer range <>) of natural;

   constant NATURAL_ARRAY_EMPTY_C : NaturalArray(0 downto 1) := (others => 0);

end package BasicPkg;

package body BasicPkg is

   function numBits(constant x : in integer) return integer is
   begin
      if ( x = 0 ) then return 1; end if;
      return integer( floor( log2( real( x ) ) ) ) + 1;
   end function numBits;

   function toSl(constant x : in boolean) return std_logic is
   begin
      if ( x ) then return '1'; else return '0'; end if;
   end function toSl;

   function ite(constant x : in boolean; constant a,b: in std_logic) return std_logic is
   begin
      if ( x ) then return a; else return b; end if;
   end function ite;

   function ite(constant x : in boolean; constant a,b: in integer) return integer is
   begin
      if ( x ) then return a; else return b; end if;
   end function ite;

   function toSlv(constant a : in Slv8Array)
   return std_logic_vector is
      variable v : std_logic_vector(8*a'length - 1 downto 0);
   begin
      for i in a'low to a'high loop
         v(8*(i - a'low) + 7 downto 8*(i - a'low)) := a(i);
      end loop;
      return v;
   end function toSlv;

end package body BasicPkg;
