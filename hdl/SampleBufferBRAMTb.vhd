configuration SampleBufferBRAMTb of SampleBufferTbNotbound is
   for sim
      for U_DUT : SampleBuffer use entity work.SampleBufferBRAM;
      end for;
   end for;
end configuration SampleBufferBRAMTb;
