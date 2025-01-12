configuration SampleBufferSDRAMTb of SampleBufferTbNotbound is
   for sim
      for U_DUT : SampleBuffer
         use entity work.SampleBufferSDRAM;
      end for;
      for U_RAM : RamEmul
         use entity work.RamEmul;
      end for;
   end for;
end configuration SampleBufferSDRAMTb;
