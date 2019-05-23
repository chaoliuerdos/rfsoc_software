The project need to setup correctly in Xilinx SDK - both hardware platform and board support package. The FPGA firmware of the DSP should be replaced by the real dsp in the short future.

The GbE was implemented based on the FreeRTOS LwIP UDP Perf Client example design.

rfdc_clk.c configures the rfdc ip - including programing LMX PLLs (there are two PLLs on the board).

udp_perf_client.c manages the dma data movement. Modification required later - the data will fill the speciifed range of DDR and not updating since the data transfer for now is too fast to be consumed by the GbE. 
