/******************************************************************************
*
* Copyright Oxford
*
******************************************************************************/

/* Connection handle for a UDP Client session */

#include "udp_perf_client.h"

#include "stdio.h"
#include "xaxidma.h"
#include "xparameters.h"
#include "xdebug.h"

#ifdef __aarch64__
#include "xil_mmu.h"
#endif

void rfdc(void);

extern struct netif server_netif;
static struct perf_stats client;
static u16 send_buf[UDP_SEND_BUFSIZE];
static struct sockaddr_in addr;
static int sock[NUM_OF_PARALLEL_CLIENTS];
#define FINISH	1
/* Report interval time in ms */
#define REPORT_INTERVAL_TIME (INTERIM_REPORT_INTERVAL * 1000)
/* End time in ms */
#define END_TIME (UDP_TIME_INTERVAL * 1000)


#define DMA_DEV_ID		XPAR_AXIDMA_0_DEVICE_ID

#ifdef XPAR_AXI_7SDDR_0_S_AXI_BASEADDR
#define DDR_BASE_ADDR		XPAR_AXI_7SDDR_0_S_AXI_BASEADDR
#elif XPAR_MIG7SERIES_0_BASEADDR
#define DDR_BASE_ADDR	XPAR_MIG7SERIES_0_BASEADDR
#elif XPAR_MIG_0_BASEADDR
#define DDR_BASE_ADDR	XPAR_DDR4_0_BASEADDR
#elif XPAR_PSU_DDR_0_S_AXI_BASEADDR
#define DDR_BASE_ADDR	XPAR_PSU_DDR_0_S_AXI_BASEADDR
#endif

#ifndef DDR_BASE_ADDR
#warning CHECK FOR THE VALID DDR ADDRESS IN XPARAMETERS.H, \
			DEFAULT SET TO 0x01000000
#define MEM_BASE_ADDR		0x01000000
#else
#define MEM_BASE_ADDR		(DDR_BASE_ADDR + 0x1000000)
#endif

#define TX_BD_SPACE_BASE	(MEM_BASE_ADDR)
#define TX_BD_SPACE_HIGH	(MEM_BASE_ADDR + 0x00000FFF)
#define RX_BD_SPACE_BASE	(MEM_BASE_ADDR + 0x00001000)
#define RX_BD_SPACE_HIGH	(MEM_BASE_ADDR + 0x00001FFF)
#define TX_BUFFER_BASE		(MEM_BASE_ADDR + 0x00100000)
#define RX_BUFFER_BASE		(MEM_BASE_ADDR + 0x00300000)
#define RX_BUFFER_HIGH		(MEM_BASE_ADDR + 0x004FFFFF)


#define MAX_PKT_LEN		0x1000
#define MARK_UNCACHEABLE        0x701
#define TEST_START_VALUE	0xC

static int RxSetup(XAxiDma * AxiDmaInstPtr);
//static int TxSetup(XAxiDma * AxiDmaInstPtr);
//static int SendPacket(XAxiDma * AxiDmaInstPtr);
static int CheckData(void);
static int CheckDmaResult(XAxiDma * AxiDmaInstPtr);

/************************** Variable Definitions *****************************/
/*
 * Device instance definitions
 */
XAxiDma AxiDma;

/*
 * Buffer for transmit packet. Must be 32-bit aligned to be used by DMA.
 */
u32 *Packet = (u32 *) TX_BUFFER_BASE;


void print_app_header(void)
{
	xil_printf("UDP client connecting to %s on port %d\r\n",
			UDP_SERVER_IP_ADDRESS, UDP_CONN_PORT);
	xil_printf("On Host: Run $iperf -s -i %d -u\r\n\r\n",
			INTERIM_REPORT_INTERVAL);
}

static void print_udp_conn_stats(void)
{
	xil_printf("[%3d] local %s port %d connected with ",
			client.client_id, inet_ntoa(server_netif.ip_addr),
			UDP_CONN_PORT);
	xil_printf("%s port %d\r\n", UDP_SERVER_IP_ADDRESS,
			UDP_CONN_PORT);
	xil_printf("[ ID] Interval\t\tTransfer   Bandwidth\n\r");
}

static void stats_buffer(char* outString,
		double data, enum measure_t type)
{
	int conv = KCONV_UNIT;
	const char *format;
	double unit = 1024.0;

	if (type == SPEED)
		unit = 1000.0;

	while (data >= unit && conv <= KCONV_GIGA) {
		data /= unit;
		conv++;
	}

	/* Fit data in 4 places */
	if (data < 9.995) { /* 9.995 rounded to 10.0 */
		format = "%4.2f %c"; /* #.## */
	} else if (data < 99.95) { /* 99.95 rounded to 100 */
		format = "%4.1f %c"; /* ##.# */
	} else {
		format = "%4.0f %c"; /* #### */
	}
	sprintf(outString, format, data, kLabel[conv]);
}


/* The report function of a UDP client session */
static void udp_conn_report(u64_t diff,
		enum report_type report_type)
{
	u64_t total_len;
	double duration, bandwidth = 0;
	char data[16], perf[16], time[64];

	if (report_type == INTER_REPORT) {
		total_len = client.i_report.total_bytes;
	} else {
		client.i_report.last_report_time = 0;
		total_len = client.total_bytes;
	}

	/* Converting duration from milliseconds to secs,
	 * and bandwidth to bits/sec .
	 */
	duration = diff / 1000.0; /* secs */
	if (duration)
		bandwidth = (total_len / duration) * 8.0;

	stats_buffer(data, total_len, BYTES);
	stats_buffer(perf, bandwidth, SPEED);

	/* On 32-bit platforms, xil_printf is not able to print
	 * u64_t values, so converting these values in strings and
	 * displaying results
	 */
	sprintf(time, "%4.1f-%4.1f sec",
			(double)client.i_report.last_report_time,
			(double)(client.i_report.last_report_time + duration));
	xil_printf("[%3d] %s  %sBytes  %sbits/sec\n\r", client.client_id,
			time, data, perf);

	if (report_type == INTER_REPORT)
		client.i_report.last_report_time += duration;
	else
		xil_printf("[%3d] sent %llu datagrams\n\r",
				client.client_id, client.cnt_datagrams);
}


static void reset_stats(void)
{
	client.client_id++;

	/* Print connection statistics */
	print_udp_conn_stats();

	/* Save start time for final report */
	client.start_time = sys_now() * portTICK_RATE_MS;
	client.total_bytes = 0;
	client.cnt_datagrams = 0;

	/* Initialize Interim report paramters */
	client.i_report.start_time = 0;
	client.i_report.total_bytes = 0;
	client.i_report.last_report_time = 0;
}

static int udp_packet_send(u8_t finished)
{
	static int packet_id;
	int i, count, *payload;
	u8_t retries = MAX_SEND_RETRY;
	socklen_t len = sizeof(addr);

	payload = (int*) (send_buf);
	if (finished == FINISH)
		packet_id = -1;
	//*payload = htonl(packet_id);

	/* always increment the id */
	packet_id++;

	for (i = 0; i < NUM_OF_PARALLEL_CLIENTS; i++) {
		while (retries) {
			usleep(10000);
			count = lwip_sendto(sock[i], send_buf, sizeof(send_buf), 0,
					(struct sockaddr *)&addr, len);
			if (count <= 0) {
				retries--;
				usleep(ERROR_SLEEP);
			} else {
				client.total_bytes += count;
				client.cnt_datagrams++;
				client.i_report.total_bytes += count;
				break;
			}
		}

		if (!retries) {
			/* Terminate this app */
			u64_t now = sys_now() * portTICK_RATE_MS;
			u64_t diff_ms = now - client.start_time;
			xil_printf("Too many udp_send() retries, ");
			xil_printf("Terminating application\n\r");
			udp_conn_report(diff_ms, UDP_DONE_CLIENT);
			xil_printf("UDP test failed\n\r");
			return -1;
		}
		retries = MAX_SEND_RETRY;

		/* For ZynqMP GEM, At high speed, packets are being
		 * recieved as Out of order at Iperf server running
		 * on remote host machine.
		 * To avoid this, added delay of 1us between each
		 * packets
		 */
#if defined (__aarch64__) && defined (XLWIP_CONFIG_INCLUDE_GEM)
		usleep(1);
#endif /* __aarch64__ */
	}
	return 0;
}

/* Transmit data on a udp session */
int transfer_data(void)
{
	/*if (END_TIME || REPORT_INTERVAL_TIME) {
		u64_t now = sys_now() * portTICK_RATE_MS;
		if (REPORT_INTERVAL_TIME) {
			if (client.i_report.start_time) {
				u64_t diff_ms = now - client.i_report.start_time;
				if (diff_ms >= REPORT_INTERVAL_TIME) {
					udp_conn_report(diff_ms, INTER_REPORT);
					client.i_report.start_time = 0;
					client.i_report.total_bytes = 0;
				}
			} else {
				client.i_report.start_time = now;
			}
		}

		if (END_TIME) {
			// this session is time-limited
			u64_t diff_ms = now - client.start_time;
			if (diff_ms >= END_TIME) {
				//time specified is over,
				//close the connection
				udp_packet_send(FINISH);
				udp_conn_report(diff_ms, UDP_DONE_CLIENT);
				xil_printf("UDP test passed Successfully\n\r");
				return FINISH;
			}
		}
	}*/

	if (udp_packet_send(!FINISH) < 0)
		return FINISH;

	return 0;
}

void start_application(void)
{
	err_t err;
	u32_t i;

	memset(&addr, 0, sizeof(struct sockaddr_in));
	addr.sin_family = AF_INET;
	addr.sin_port = htons(UDP_CONN_PORT);
	addr.sin_addr.s_addr = inet_addr(UDP_SERVER_IP_ADDRESS);

	for (i = 0; i < NUM_OF_PARALLEL_CLIENTS; i++) {
		if ((sock[i] = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
			xil_printf("UDP client: Error creating Socket\r\n");
			return;
		}

		err = connect(sock[i], (struct sockaddr *)&addr, sizeof(addr));
		if (err != ERR_OK) {
			xil_printf("UDP client: Error on connect: %d\r\n", err);
			close(sock[i]);
			return;
		}
	}

	/* Wait for successful connections */
	usleep(10);

	reset_stats();
	rfdc();
	dma_data_move();
	/* initialize data buffer being sent with same as used in iperf */
	//for (i = 0; i < UDP_SEND_BUFSIZE; i++)
		//send_buf[i] = i ;
		//send_buf[i] = (i % 10) + '0';

	while (!transfer_data());
}

void dma_data_move(void)
{
	int Status;
	XAxiDma_Config *Config;


	xil_printf("\r\n--- Entering main() --- \r\n");
#ifdef __aarch64__
	Xil_SetTlbAttributes(RX_BD_SPACE_BASE, MARK_UNCACHEABLE);
#endif

	Config = XAxiDma_LookupConfig(DMA_DEV_ID);
	if (!Config) {
		xil_printf("No config found for %d\r\n", DMA_DEV_ID);

		return XST_FAILURE;
	}

	/* Initialize DMA engine */
	Status = XAxiDma_CfgInitialize(&AxiDma, Config);
	if (Status != XST_SUCCESS) {
		xil_printf("Initialization failed %d\r\n", Status);
		return XST_FAILURE;
	}

	if(!XAxiDma_HasSg(&AxiDma)) {
		xil_printf("Device configured as Simple mode \r\n");

		return XST_FAILURE;
	}


	Status = RxSetup(&AxiDma);
	if (Status != XST_SUCCESS) {
		return XST_FAILURE;
	}
	xil_printf("Rx Setup completed ... \r\n");



	CheckDmaResult(&AxiDma);

	if (Status != XST_SUCCESS) {
		xil_printf("AXI DMA SG Polling Example Failed\r\n");
		return XST_FAILURE;
	}

	xil_printf("Successfully ran AXI DMA SG Polling Example\r\n");
	xil_printf("--- Exiting main() --- \r\n");

	if (Status != XST_SUCCESS) {
		return XST_FAILURE;
	}

	return XST_SUCCESS;
}

#if defined(XPAR_UARTNS550_0_BASEADDR)
/*****************************************************************************/
/*
*
* Uart16550 setup routine, need to set baudrate to 9600, and data bits to 8
*
* @param	None
*
* @return	None
*
* @note		None.
*
******************************************************************************/
static void Uart550_Setup(void)
{

	/* Set the baudrate to be predictable
	 */
	XUartNs550_SetBaud(XPAR_UARTNS550_0_BASEADDR,
			XPAR_XUARTNS550_CLOCK_HZ, 9600);

	XUartNs550_SetLineControlReg(XPAR_UARTNS550_0_BASEADDR,
			XUN_LCR_8_DATA_BITS);

}
#endif

/*****************************************************************************/
/**
*
* This function sets up RX channel of the DMA engine to be ready for packet
* reception
*
* @param	AxiDmaInstPtr is the pointer to the instance of the DMA engine.
*
* @return	XST_SUCCESS if the setup is successful, XST_FAILURE otherwise.
*
* @note		None.
*
******************************************************************************/
static int RxSetup(XAxiDma * AxiDmaInstPtr)
{
	XAxiDma_BdRing *RxRingPtr;
	int Delay = 0;
	int Coalesce = 1;
	int Status;
	XAxiDma_Bd BdTemplate;
	XAxiDma_Bd *BdPtr;
	XAxiDma_Bd *BdCurPtr;
	u32 BdCount;
	u32 FreeBdCount;
	UINTPTR RxBufferPtr;
	int Index;

	RxRingPtr = XAxiDma_GetRxRing(&AxiDma);

	/* Disable all RX interrupts before RxBD space setup */

	XAxiDma_BdRingIntDisable(RxRingPtr, XAXIDMA_IRQ_ALL_MASK);

	/* Set delay and coalescing */
	XAxiDma_BdRingSetCoalesce(RxRingPtr, Coalesce, Delay);

	/* Setup Rx BD space */
	BdCount = XAxiDma_BdRingCntCalc(XAXIDMA_BD_MINIMUM_ALIGNMENT,
				RX_BD_SPACE_HIGH - RX_BD_SPACE_BASE + 1);
	xil_printf("aa1");

	xil_printf("BdCount %d\r\n", BdCount);
	Status = XAxiDma_BdRingCreate(RxRingPtr, RX_BD_SPACE_BASE,
				RX_BD_SPACE_BASE,
				XAXIDMA_BD_MINIMUM_ALIGNMENT, BdCount);
	xil_printf("aa2");
	if (Status != XST_SUCCESS) {
		xil_printf("RX create BD ring failed %d\r\n", Status);

		return XST_FAILURE;
	}
	xil_printf("aa3");
	/*
	 * Setup an all-zero BD as the template for the Rx channel.
	 */
	XAxiDma_BdClear(&BdTemplate);

	Status = XAxiDma_BdRingClone(RxRingPtr, &BdTemplate);
	if (Status != XST_SUCCESS) {
		xil_printf("RX clone BD failed %d\r\n", Status);

		return XST_FAILURE;
	}

	/* Attach buffers to RxBD ring so we are ready to receive packets */

	FreeBdCount = XAxiDma_BdRingGetFreeCnt(RxRingPtr);
	xil_printf("FreeBdCount r %d /\r\n", FreeBdCount);
	Status = XAxiDma_BdRingAlloc(RxRingPtr, FreeBdCount, &BdPtr);
	if (Status != XST_SUCCESS) {
		xil_printf("RX alloc BD failed %d\r\n", Status);

		return XST_FAILURE;
	}

	BdCurPtr = BdPtr;
	RxBufferPtr = RX_BUFFER_BASE;
	for (Index = 0; Index < FreeBdCount; Index++) {
		Status = XAxiDma_BdSetBufAddr(BdCurPtr, RxBufferPtr);

		if (Status != XST_SUCCESS) {
			xil_printf("Set buffer addr %x on BD %x failed %d\r\n",
			    (unsigned int)RxBufferPtr,
			    (UINTPTR)BdCurPtr, Status);

			return XST_FAILURE;
		}

		Status = XAxiDma_BdSetLength(BdCurPtr, MAX_PKT_LEN,
				RxRingPtr->MaxTransferLen);
		if (Status != XST_SUCCESS) {
			xil_printf("Rx set length %d on BD %x failed %d\r\n",
			    MAX_PKT_LEN, (UINTPTR)BdCurPtr, Status);

			return XST_FAILURE;
		}

		/* Receive BDs do not need to set anything for the control
		 * The hardware will set the SOF/EOF bits per stream status
		 */
		XAxiDma_BdSetCtrl(BdCurPtr, 0);
		XAxiDma_BdSetId(BdCurPtr, RxBufferPtr);

		RxBufferPtr += MAX_PKT_LEN;
		xil_printf("Data RxBufferPtr:  %x  \r\n",RxBufferPtr);
		BdCurPtr = (XAxiDma_Bd *)XAxiDma_BdRingNext(RxRingPtr, BdCurPtr);
	}

	/* Clear the receive buffer, so we can verify data
	 */
	memset((void *)RX_BUFFER_BASE, 0, MAX_PKT_LEN);

	Status = XAxiDma_BdRingToHw(RxRingPtr, FreeBdCount,
						BdPtr);
	if (Status != XST_SUCCESS) {
		xil_printf("RX submit hw failed %d\r\n", Status);

		return XST_FAILURE;
	}

	/* Start RX DMA channel */
	Status = XAxiDma_BdRingStart(RxRingPtr);
	if (Status != XST_SUCCESS) {
		xil_printf("RX start hw failed %d\r\n", Status);

		return XST_FAILURE;
	}

	return XST_SUCCESS;
}


/*****************************************************************************/
/**
*
* This function transmits one packet non-blockingly through the DMA engine.
*
* @param	AxiDmaInstPtr points to the DMA engine instance
*
* @return	- XST_SUCCESS if the DMA accepts the packet successfully,
*		- XST_FAILURE otherwise.
*
* @note     None.
*
******************************************************************************/


/*****************************************************************************/
/*
*
* This function checks data buffer after the DMA transfer is finished.
*
* @param	None
*
* @return	- XST_SUCCESS if validation is successful
*		- XST_FAILURE if validation is failure.
*
* @note		None.
*
******************************************************************************/
static int CheckData(void)
{
	u16 *RxPacket;
	int Index = 0;
	//u8 Value;
	//FILE * fp;
	   /* open the file for writing
	 if ((fp = fopen("D:\\RfSoC_projects\\adc_data\\a.txt","w")) == NULL){
	        printf("Error! opening file");
	    }*/

	RxPacket = (u16 *) RX_BUFFER_BASE;
	//Value = TEST_START_VALUE;

	/* Invalidate the DestBuffer before receiving the data, in case the
	 * Data Cache is enabled
	 */

	Xil_DCacheInvalidateRange((UINTPTR)RxPacket, MAX_PKT_LEN);


	for(Index = 0; Index < UDP_SEND_BUFSIZE; Index++) {
		//if (RxPacket[Index] != Value) {
			xil_printf("%X\r\n",(unsigned int)RxPacket[Index]);
			send_buf[Index]=(unsigned int)RxPacket[Index];
			//xil_printf("Data  %d: %x\r\n", Index, (unsigned int)RxPacket[Index]);
			//fprintf(fp, "%c\n",RxPacket[Index]);

		}
		//Value = (Value + 1) & 0xFF;
	//}
	//fclose(fp);
	return XST_SUCCESS;
}

/*****************************************************************************/
/**
*
* This function waits until the DMA transaction is finished, checks data,
* and cleans up.
*
* @param	None
*
* @return	- XST_SUCCESS if DMA transfer is successful and data is correct,
*		- XST_FAILURE if fails.
*
* @note		None.
*
******************************************************************************/
static int CheckDmaResult(XAxiDma * AxiDmaInstPtr)
{

	XAxiDma_BdRing *RxRingPtr;
	XAxiDma_Bd *BdPtr;
	int ProcessedBdCount= 0;
	u32 FreeBdCount;
	int Status;

	RxRingPtr = XAxiDma_GetRxRing(AxiDmaInstPtr);


	xil_printf("\r\n--- CheckDmaResult --- \r\n");


	/*Wait until the data has been received by the Rx channel
	while ((ProcessedBdCount = XAxiDma_BdRingFromHw(RxRingPtr,
						       XAXIDMA_ALL_BDS,
						       &BdPtr)) == 0) {
	}*/
	/* Wait until the data has been received by the Rx channel
	ProcessedBdCount = 0;

	while (ProcessedBdCount < 64) {

		ProcessedBdCount += XAxiDma_BdRingFromHw(RxRingPtr,
					       XAXIDMA_ALL_BDS, &BdPtr);
		xil_printf("ProcessedBdCount:  %d  \r\n",ProcessedBdCount);
	}*/


	xil_printf("\r\n--- CheckDmaResult_r --- \r\n");

	/* Check received data */
	if (CheckData() != XST_SUCCESS) {

		return XST_FAILURE;
	}

	/* Free all processed RX BDs for future transmission */
	Status = XAxiDma_BdRingFree(RxRingPtr, ProcessedBdCount, BdPtr);
	if (Status != XST_SUCCESS) {
		xil_printf("Failed to free %d rx BDs %d\r\n",
		    ProcessedBdCount, Status);
		return XST_FAILURE;
	}

	/* Return processed BDs to RX channel so we are ready to receive new
	 * packets:
	 *    - Allocate all free RX BDs
	 *    - Pass the BDs to RX channel
	 */
	FreeBdCount = XAxiDma_BdRingGetFreeCnt(RxRingPtr);
	Status = XAxiDma_BdRingAlloc(RxRingPtr, FreeBdCount, &BdPtr);
	if (Status != XST_SUCCESS) {
		xil_printf("bd alloc failed\r\n");
		return XST_FAILURE;
	}

	Status = XAxiDma_BdRingToHw(RxRingPtr, FreeBdCount, BdPtr);
	if (Status != XST_SUCCESS) {
		xil_printf("Submit %d rx BDs failed %d\r\n", FreeBdCount, Status);
		return XST_FAILURE;
	}

	return XST_SUCCESS;
}

