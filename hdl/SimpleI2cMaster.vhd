library ieee;
use ieee.std_logic_1164.all;

-- simple i2c master (no arbitration)
package SimpleI2cMasterPkg is 

   constant I2C_CMD_NONE : std_logic_vector(1 downto 0) := "00";
   constant I2C_CMD_READ : std_logic_vector(1 downto 0) := "10";

   -- read without sending ACK
   constant I2C_CMD_RNAK : std_logic_vector(1 downto 0) := "11";
   constant I2C_CMD_WRITE: std_logic_vector(1 downto 0) := "01";

   function i2cCmdIsRead(cmd: std_logic_vector(1 downto 0)) return boolean;

   type I2cReqType is record
      start : std_logic;                    -- issue START
      cmd   : std_logic_vector(1 downto 0); -- what to do
      stop  : std_logic;                    -- issue STOP (after cmd)
      wdat  : std_logic_vector(7 downto 0); -- write data
      vld   : std_logic;                    -- request valid
   end record I2cReqType;

   constant I2C_REQ_INIT_C : I2cReqType := (
      start => '0',
      cmd   => I2C_CMD_NONE,
      stop  => '0',
      wdat  => (others => '0'),
      vld   => '0'
   );

   -- the 'valid' bit must be cleared as soon as the request
   -- is taken by the controller (valid and ready cycle).
   -- the remaining items must remain latched until the controller
   -- is done.

   component SimpleI2cMaster is
      generic (
         BUS_FREQ_HZ_G : real;
         I2C_FREQ_HZ_G : real := 1.0E5
      );
      port (
         clk           : in  std_logic;
         rst           : in  std_logic;
         req           : in  I2cReqType;
         -- ready to accept a request
         -- user MUST clear 'vld' as soon as
         -- ready is seen.
         rdy           : out std_logic;
         -- rdat(0) holds ACK of a write cycle
         -- rdat(8 downto 1) is read data
         rdat          : out std_logic_vector(8 downto 0);
         don           : out std_logic;

         sclInp        : in  std_logic;
         sclOut        : out std_logic;
         sdaInp        : in  std_logic;
         sdaOut        : out std_logic
      );
   end component SimpleI2cMaster;

end package SimpleI2cMasterPkg;

package body SimpleI2cMasterPkg is

   function i2cCmdIsRead(cmd: std_logic_vector(1 downto 0)) return boolean is
   begin
      return cmd(1) = '1';
   end function i2cCmdIsRead;

end package body SimpleI2cMasterPkg;


library ieee;
use ieee.std_logic_1164.all;

use work.SimpleI2cMasterPkg.all;
use ieee.math_real.all;
use ieee.numeric_std.all;

entity SimpleI2cMaster is
      generic (
         BUS_FREQ_HZ_G : real;
         I2C_FREQ_HZ_G : real := 1.0E5
      );
      port (
         clk           : in  std_logic;
         rst           : in  std_logic;
         req           : in  I2cReqType;

         -- ready to accept a request
         -- user MUST clear 'vld' as soon as
         -- ready is seen.
         rdy           : out std_logic;
         rdat          : out std_logic_vector(8 downto 0);
         don           : out std_logic;

         sclInp        : in  std_logic;
         sclOut        : out std_logic;
         sdaInp        : in  std_logic;
         sdaOut        : out std_logic
      );
end entity SimpleI2cMaster;

architecture rtl of SimpleI2cMaster is

   constant HALF_PER_C : integer := integer(ceil(BUS_FREQ_HZ_G/I2C_FREQ_HZ_G/2.0));
   constant DIV_W_C    : integer := integer(ceil(log2(real(HALF_PER_C))));

   constant COUNT_C    : unsigned(DIV_W_C - 1 downto 0) := to_unsigned(HALF_PER_C - 1, DIV_W_C);

   type StateType is (IDLE, CHECK, RESTART, START, SHIFT, STOP);

   type RegType is record
      sdaOut : std_logic;
      sclOut : std_logic;
      state  : StateType;
      dlyCnt : unsigned(DIV_W_C - 1 downto 0);
      bitCnt : unsigned(          3 downto 0);
      stop   : std_logic;
      cmd    : std_logic_vector(1 downto 0);
      rdy    : std_logic;
      don    : std_logic;
      data   : std_logic_vector(8 downto 0);
   end record RegType;

   constant REG_INIT_C : RegType := (
      sdaOut => '1',
      sclOut => '1',
      state  => IDLE,
      dlyCnt => (others => '0'),
      bitCnt => (others => '0'),
      stop   => '0',
      cmd    => I2C_CMD_NONE,
      rdy    => '0',
      don    => '0',
      data   => (others => '1')
   );

   signal r       : RegType := REG_INIT_C;
   signal rin     : RegType;

begin

   P_COMB : process (r, req, sdaInp, sclInp) is
      variable v : RegType;
   begin
      v := r;

      v.rdy := '0';
      v.don := '0';

      case ( r.state ) is
         when IDLE =>
            if ( r.rdy = '1' and req.vld = '1' ) then
               -- latch command
               v.cmd  := req.cmd;
               v.stop := req.stop;
               if ( i2cCmdIsRead(req.cmd) ) then
                  v.data(8 downto 1) := (others => '1');
                  v.data(0)          := req.cmd(0); -- ACK/NACK
               else
                  v.data := req.wdat & '1';
               end if;
               if ( req.start = '1' ) then
                  if ( r.sclOut = '0' ) then
                     if ( r.sdaOut = '0' ) then
                        v.sdaOut := '1';
                        v.dlyCnt := COUNT_C;
                     end if;
                     v.state  := RESTART;
                  else
                     v.sdaOut := '0';
                     v.dlyCnt := COUNT_C;
                     v.state  := START;
                  end if;
               else
                  v.state  := CHECK;
               end if;
            else
               v.rdy := '1';
            end if;

         when RESTART =>
            if ( r.dlyCnt = 0 ) then
               if ( r.sclOut = '0' ) then
                  v.sclOut := '1';
                  v.dlyCnt := COUNT_C;
               else
                  v.sdaOut := '0';
                  v.dlyCnt := COUNT_C;
                  v.state  := START;
               end if;
            else
               -- handle clock stretching here
               if ( r.sclOut = '0' or sclInp = '1' ) then
                  v.dlyCnt := r.dlyCnt - 1;
               end if;
            end if;
               
         when CHECK =>
            if ( r.cmd /= I2C_CMD_NONE ) then
               -- READ/WRITE  (we already own the bus)
               v.bitCnt := to_unsigned(8, r.bitCnt'length);
               v.dlyCnt := COUNT_C;
               v.state  := SHIFT;
            elsif ( r.stop   = '1' ) then
               -- stop
               v.dlyCnt := COUNT_C;
               v.state  := STOP;
            else
               v.don    := '1';
               v.state  := IDLE;
            end if;

         when START =>
            if ( r.dlyCnt= 0 ) then
               v.sclOut := '0';
               v.state  := CHECK;
            else
               v.dlyCnt:= r.dlyCnt- 1;
            end if;
                  
         when STOP  =>
            -- handle clock stretching here
            if ( r.sclOut = '0' or sclInp = '1' ) then
               v.dlyCnt := r.dlyCnt - 1;
            end if;

            if ( r.dlyCnt(r.dlyCnt'left - 1 downto 0) = 0 ) then
               -- half a half-clock period has expired; it is now safe
               -- to clear SDA to prepare for the stop condition
               if ( r.sdaOut = '1' and r.sclOut = '0' ) then
                  v.sdaOut := '0';
               elsif ( r.dlyCnt(r.dlyCnt'left) = '0' ) then
                  -- a full half-clock period has expired
                  if ( r.sdaOut = '1' ) then
                     -- we are done
                     v.stop   := '0';
                     v.state  := CHECK;
                     v.dlyCnt := (others => '0');
                  elsif ( r.sclOut = '0' ) then
                     -- we are about to raise SCL (SDA has been low)
                     v.sclOut := '1';
                     v.dlyCnt := COUNT_C;
                  else
                     -- SCL is high; raise SDA and wait
                     v.sdaOut := '1';
                     v.dlyCnt := COUNT_C;
                  end if;
               end if;
            end if;

         when SHIFT =>

            -- handle clock stretching here
            if ( r.sclOut = '0' or sclInp = '1' ) then
               v.dlyCnt := r.dlyCnt - 1;
            end if;

            if ( r.dlyCnt(r.dlyCnt'left - 1 downto 0) = 0 ) then
               if ( r.sclOut = '0' ) then
                  if ( r.dlyCnt(r.dlyCnt'left) = '0' ) then
                     v.sclOut := '1';
                     v.dlyCnt := COUNT_C;
                     -- the 9th bit is ACK
                     v.data   := r.data(r.data'left - 1 downto 0) & sdaInp;
                  else
                     v.sdaOut := r.data(8);
                  end if;
               else
                  if ( r.dlyCnt(r.dlyCnt'left) = '0' ) then
                     v.sclOut := '0';
                     if ( r.bitCnt = 0 ) then
                        v.cmd     := I2C_CMD_NONE;
                        v.state   := CHECK;
                        v.dlyCnt  := (others => '0');
                     else
                        v.bitCnt := r.bitCnt - 1;
                        -- shift next data out
                        v.dlyCnt := COUNT_C;
                     end if;
                  end if;
               end if;
            end if;

      end case;

      rin <= v;
   end process P_COMB;

   P_SEQ : process ( clk ) is
   begin
      if ( rising_edge( clk ) ) then
         if ( rst = '1' ) then
            r <= REG_INIT_C;
         else
            r <= rin;
         end if;
      end if;
   end process P_SEQ;

   rdy    <= r.rdy;
   don    <= r.don;
   rdat   <= r.data;

   sdaOut <= r.sdaOut;
   sclOut <= r.sclOut;
end architecture rtl;
