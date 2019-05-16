/*****************************************************************************/
#define XPS_BOARD_ZCU111
/***************************** Include Files ********************************/
#ifdef __BAREMETAL__
#include "xparameters.h"
#endif

#include "xrfdc.h"
//#include "xaxidma_example_sg_poll.c"
#ifdef XPS_BOARD_ZCU111
#include "xrfdc_clk.h"
#include "xrfdc_clk.c"
#endif
void dma_data_move();
/************************** Constant Definitions ****************************/

/*
 * The following constants map to the XPAR parameters created in the
 * xparameters.h file. They are defined here such that a user can easily
 * change all the needed parameters in one place.
 */
#ifdef __BAREMETAL__
#define BUS_NAME        "generic"
#define RFDC_DEV_NAME    XPAR_XRFDC_0_DEV_NAME
#define XRFDC_BASE_ADDR		XPAR_XRFDC_0_BASEADDR
#define RFDC_DEVICE_ID 	XPAR_XRFDC_0_DEVICE_ID
#else
#define BUS_NAME        "platform"
#define RFDC_DEVICE_ID 	0
#endif


/**************************** Type Definitions ******************************/


/***************** Macros (Inline Functions) Definitions ********************/
#ifdef __BAREMETAL__
#define printf xil_printf


#endif
/************************** Function Prototypes *****************************/


#ifdef __BAREMETAL__
int register_metal_device(void);
#endif

/************************** Variable Definitions ****************************/

static XRFdc RFdcInst;
#ifdef XPS_BOARD_ZCU111
unsigned int LMK04208_CKin[1][26] = {
		{0x00160040,0x80140320,0x80140321,0x80140322,
		0xC0140023,0x40140024,0x80141E05,0x03300006,0x01300007,0x06010008,
		0x55555549,0x9102410A,0x0401100B,0x1B0C006C,0x2302886D,0x0200000E,
		0x8000800F,0xC1550410,0x00000058,0x02C9C419,0x8FA8001A,0x10001E1B,
		0x0021201C,0x0180033D,0x0200033E,0x003F001F }};
#endif

void rfdc(void)
{
	int Status;
	u16 Tile;
    u16 Block;
    XRFdc *RFdcInstPtr = &RFdcInst;
	XRFdc_Config *ConfigPtr;
	XRFdc_BlockStatus BlockStatus;
	XRFdc_BlockStatus PLLStatus;
	XRFdc_IPStatus myIPStatus;

	printf("RFdc Selftest Example Test\r\n");
	/*
	 * Run the RFdc Decoder Mode example, specify the Device ID that is
	 * generated in xparameters.h.
	 */
	/* Initialize the RFdc driver. */
     	ConfigPtr = XRFdc_LookupConfig(RFDC_DEVICE_ID);
		if (ConfigPtr == NULL) {
			return XRFDC_FAILURE;
		}

		Status = XRFdc_CfgInitialize(RFdcInstPtr, ConfigPtr);
		if (Status != XRFDC_SUCCESS) {
			return XRFDC_FAILURE;
		}
#ifdef XPS_BOARD_ZCU111
printf("\n Configuring the Clock \r\n");
#ifdef __BAREMETAL__
	LMK04208ClockConfig(1, LMK04208_CKin);
	LMX2594ClockConfig(1, 4096000);
	printf("\n Configured the Clock \r\n");
#else
	LMK04208ClockConfig(12, LMK04208_CKin);
	LMX2594ClockConfig(12, 4096000);
	printf("\n Configured the Clock bb \r\n");
#endif
#endif

	sleep(10);

	 Status = XRFdc_GetIPStatus(RFdcInstPtr, &myIPStatus);
		      if (Status != XRFDC_SUCCESS) {
			  return XRFDC_FAILURE;
			  printf("\n Get IP statue \r\n");
			  }

  int powerup_status;
    int tile_state;
    powerup_status = myIPStatus.ADCTileStatus[0].PowerUpState;
    tile_state = myIPStatus.ADCTileStatus[0].TileState;

    printf("ADC PowerUp Status: %u\n", powerup_status);
    printf("ADC Tile State: %u\n", tile_state);

    int whole, thousandths;  // will need these for the doubles not supported by xil_printf

      /*print out the ADC status*/
   	Status = XRFdc_GetBlockStatus(RFdcInstPtr, XRFDC_ADC_TILE, 0, 0, &BlockStatus);
  					if (Status != XRFDC_SUCCESS) {
  						return XRFDC_FAILURE;
  					}
  	Status = XRFdc_GetPLLLockStatus(RFdcInstPtr, XRFDC_ADC_TILE, 0, &PLLStatus);
    printf("PLL State: %d\n", Status);
      whole = BlockStatus.SamplingFreq;
      thousandths = (BlockStatus.SamplingFreq - whole) * 1000;
      printf("ADC Sampling Frequency: %d.%3d\n",whole,thousandths);

      printf("\n ADC%d%d Status \n"
      	"AnalogDataPathStatus - %u \nDigitalDataPathStatus - %u\nDataPathClockStatus - %u\nIsFIFOFlagsEnabled - %u\nIsFIFOFlagsAsserted - %u\n", 0, 1, BlockStatus.AnalogDataPathStatus, BlockStatus.DigitalDataPathStatus, BlockStatus.DataPathClocksStatus,
      	 BlockStatus.IsFIFOFlagsEnabled, BlockStatus.IsFIFOFlagsAsserted);


Tile = 0x0;
for (Block = 0; Block <4; Block++) {
	if (XRFdc_IsADCBlockEnabled(RFdcInstPtr, Tile, Block)) {
	             	printf("\n tile %d block %d is available \r\n", Tile, Block);
					if (RFdcInstPtr->ADC4GSPS == XRFDC_ADC_4GSPS) {
						if ((Block == 2) || (Block == 3))
							continue;
						else if (Block == 1) {
							if (XRFdc_IsADCBlockEnabled(RFdcInstPtr, Tile, 2) == 0)
								continue;
						}
					}
	}
}
	xil_printf("Successfully ran Selftest Example Test\r\n");

	//dma_data_move();
	return XRFDC_SUCCESS;
}

