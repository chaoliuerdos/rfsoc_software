/*
 * xrfdc_clk.h
 *
 *  Created on: 20 Mar 2019
 *      Author: chaoliu
 */
#ifndef RFDC_CLK_H_
#define RFDC_CLK_H_
#ifdef XPS_BOARD_ZCU111

void LMX2594ClockConfig(int XIicBus, int XFrequency);
void LMK04208ClockConfig(int XIicBus, unsigned int LMK04208_CKin[1][26]);

#endif /* XPS_BOARD_ZCU111 */
#endif /* RFDC_CLK_H_ */
