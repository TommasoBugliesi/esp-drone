// Copyright 2015-2019 Espressif Systems (Shanghai) PTE LTD
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

// Adapted by Tommaso Bugliesi for ESP-Drone

#include <stdbool.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "stm32_legacy.h"
#include "motors_brushless.h"
#include "log.h"
#define DEBUG_MODULE "MOTORS"
#include "debug_cf.h"

static double core_ticks_per_bit;     //  This line calculates the number of clock ticks per bit for the DSHOT protocol.
static uint16_t dt_t0h, dt_t0l;       //  Ticks duration to stay low and high for a 0
static uint16_t dt_t1h, dt_t1l;       //  Ticks duration to stay low and high for a 1
static uint16_t dt_tpb;               //  Total duration of a bit (ticks per bit)
static uint16_t dt_pause;             //  Pause between bits

typedef struct dshot_packet_t{
    uint16_t payload;
    bool telemetry;
} dshot_packet_t;

static uint32_t motor_ratios[4] = {0, 0, 0, 0};

/**
 * DHSOTn is a protocol that works at a frequency n mentioned in the name of the protocol.
 * DHOT600 -> 600kHz (number of bits per seconds)
 * The RMT channel uses the APB Clock (80MHz) to generate the clock signal. A clock diviver can be used to reduce the number of ticks per bit   
 * 
 */
rmt_config_t config[NBR_OF_MOTORS] = {
    {    
        .channel = RMT_CHANNEL_1,                      // rmt channel to be linked to gpio
        .rmt_mode = RMT_MODE_TX,                     // rmt as output
        .gpio_num = MOTOR1_GPIO,                     // GPIO number 
        .mem_block_num = 1,                          // Link memory block to channel 
        .clk_div = RMT_DIVIDER,                      // The RMT source clock is typically APB CLK, 80Mhz by default. But when RMT_CHANNEL_FLAGS_AWARE_DFS is set in flags, RMT source clock is changed to REF_TICK or XTAL.
        .tx_config.loop_en = false,                  // Transmit data only once
        .tx_config.carrier_en = false,               // No carrier wave
        .tx_config.idle_level = RMT_IDLE_LEVEL_HIGH,    
        .tx_config.idle_output_en = true,
        .flags = RMT_CHANNEL_FLAGS_INVERT_SIG,
    },    
    {    
        .channel = RMT_CHANNEL_2,                      // rmt channel to be linked to gpio
        .rmt_mode = RMT_MODE_TX,                     // rmt as output
        .gpio_num = MOTOR2_GPIO,                     // GPIO number 
        .mem_block_num = 1,                          // Link memory block to channel 
        .clk_div = RMT_DIVIDER,                      // The RMT source clock is typically APB CLK, 80Mhz by default. But when RMT_CHANNEL_FLAGS_AWARE_DFS is set in flags, RMT source clock is changed to REF_TICK or XTAL.
        .tx_config.loop_en = false,                  // Transmit data only once
        .tx_config.carrier_en = false,               // No carrier wave
        .tx_config.idle_level = RMT_IDLE_LEVEL_HIGH,    
        .tx_config.idle_output_en = true,
        .flags = RMT_CHANNEL_FLAGS_INVERT_SIG,
    },
        {    
        .channel = RMT_CHANNEL_3,                      // rmt channel to be linked to gpio
        .rmt_mode = RMT_MODE_TX,                     // rmt as output
        .gpio_num = MOTOR3_GPIO,                     // GPIO number 
        .mem_block_num = 1,                          // Link memory block to channel 
        .clk_div = RMT_DIVIDER,                      // The RMT source clock is typically APB CLK, 80Mhz by default. But when RMT_CHANNEL_FLAGS_AWARE_DFS is set in flags, RMT source clock is changed to REF_TICK or XTAL.
        .tx_config.loop_en = false,                  // Transmit data only once
        .tx_config.carrier_en = false,               // No carrier wave
        .tx_config.idle_level = RMT_IDLE_LEVEL_HIGH,    
        .tx_config.idle_output_en = true,
        .flags = RMT_CHANNEL_FLAGS_INVERT_SIG,
    },
        {    
        .channel = RMT_CHANNEL_4,                      // rmt channel to be linked to gpio
        .rmt_mode = RMT_MODE_TX,                     // rmt as output
        .gpio_num = MOTOR4_GPIO,                     // GPIO number 
        .mem_block_num = 1,                          // Link memory block to channel 
        .clk_div = RMT_DIVIDER,                      // The RMT source clock is typically APB CLK, 80Mhz by default. But when RMT_CHANNEL_FLAGS_AWARE_DFS is set in flags, RMT source clock is changed to REF_TICK or XTAL.
        .tx_config.loop_en = false,                  // Transmit data only once
        .tx_config.carrier_en = false,               // No carrier wave
        .tx_config.idle_level = RMT_IDLE_LEVEL_HIGH,    
        .tx_config.idle_output_en = true,
        .flags = RMT_CHANNEL_FLAGS_INVERT_SIG,
    },
};

static bool isInit = false;


/* Functions Prorotypes Start*/
void DshotInit(void);
void DshotReset(void);
void DshotSendThrottle(uint8_t channel, uint16_t ithrust);
// void DshotDeinit();
void DshotSetData(uint16_t data);
void DshotWriteData(uint8_t channel, uint16_t data, bool wait);
uint8_t DshotChecksum(uint16_t data);
void DshotWritePacket(uint8_t channel, dshot_packet_t packet, bool wait);
void DshotRepeatPacket(uint8_t channel, dshot_packet_t packet, int n);
void DshotSetReversed(bool reversed);
/* Functions Prorotypes End*/


/* Public functions */

//Initialization. Will set all motors ratio to 0%
void motorsBrushlessInit()
{
    if (isInit) {
        // First to init will configure it
        return;
    }

    DshotInit();
    DshotReset();
    DshotSetReversed(false);

    int armCounter;
    for (armCounter=0; armCounter<50; armCounter++){
       motorsBrushlessApplyAll(DSHOT_THROTTLE_MIN, DSHOT_THROTTLE_MIN, DSHOT_THROTTLE_MIN, DSHOT_THROTTLE_MIN);  
       armCounter ++;   
    }

    isInit = true;
}

bool motorsBrushlessTest(void)
{
    // TODO : Function to test motors

    return true;
}

/**
 * Update the motors driver
 */
void motorsBrushlessApplyAll(uint16_t ithrust1, uint16_t ithrust2, uint16_t ithrust3, uint16_t ithrust4){
    DshotSendThrottle(MOTOR_M1, ithrust1);
    DshotSendThrottle(MOTOR_M2, ithrust2);
    DshotSendThrottle(MOTOR_M3, ithrust3);
    DshotSendThrottle(MOTOR_M4, ithrust4);
}

/**
 * Update the motors driver
 */
void motorsBrushlessApplyChannel(uint8_t channel, uint16_t ithrust){
    DshotSendThrottle(channel, ithrust);
}

/**
 * Get a single motor driver
 */
int motorsBrushlessGetChannel(uint8_t id){
    return motor_ratios[id];
}

/* Private functions */
void DshotInit(void){
    // Calculate bit timing for RMT peripheral
	core_ticks_per_bit = APB_CLK_FREQ/DSHOT_FREQUENCY;   //This line calculates the number of clock ticks per bit for the DSHOT protocol.
    dt_tpb = core_ticks_per_bit/RMT_DIVIDER;               // Ticks per bit
	dt_t0h = core_ticks_per_bit/RMT_DIVIDER/3;             // Ticks per bit 0 High 
	dt_t1h = core_ticks_per_bit/RMT_DIVIDER*2/3;           // Ticks per bit 1 High
	dt_t0l = dt_tpb-dt_t0h;                               // Ticks per bit 0 Low
	dt_t1l = dt_tpb-dt_t1h;                               // Ticks per bit 1 Low 
	dt_pause = dt_tpb*200;

    esp_err_t rslt;

    // Assign rmtChannel to internal object variable
    for (int h = 0; h<NBR_OF_MOTORS; h++){        
        rslt = rmt_config(&config[h]);               
        
        if (rslt != ESP_OK) {
            DEBUG_PRINTE("RMT config failed: %s", esp_err_to_name(rslt));
            return;
        }
        
        rslt = rmt_driver_install(config[h].channel, 0, 0);
        if (rslt != ESP_OK) {
            DEBUG_PRINTE("RMT config failed: %s", esp_err_to_name(rslt));
            return;
        }
    }
}

// esp_err_t MotorControl::DShotDeinit(){
//   esp_err_t out[4];
//   for (int h = 0; h<4; h++){
//     out[h] = rmt_driver_uninstall(_rmtChannel[h]);
//     _rmtChannel[h] = RMT_CHANNEL_MAX;
//   }
//   return *out;
// }

void DshotReset(){
    uint16_t data = 0;

    // Set 50 emtpy data to reset DShot device 
    for (int i = 0; i < 50; i++)
    {
        DshotWriteData(MOTOR_M1, data, true);
        DshotWriteData(MOTOR_M2, data, true);
        DshotWriteData(MOTOR_M3, data, true);
        DshotWriteData(MOTOR_M4, data, true);
    }

}

void DshotWriteData(uint8_t channel, uint16_t data, bool wait){
    esp_err_t rslt;

    // Max time to wait for TX done can be an option in case for fast control loops
    if (wait){
        rmt_wait_tx_done(MOTOR_TO_RMT_CHANNEL(channel), WAIT_FOR_TX_DONE);
    }
    else{
        rmt_wait_tx_done(MOTOR_TO_RMT_CHANNEL(channel), 0); 
    }

    // DEBUG_PRINT_LOCAL("Channel: %d", channel);

    // Assemble data in RMT format
    DshotSetData(data);

    // Send data
    rslt = rmt_write_items(MOTOR_TO_RMT_CHANNEL(channel), _dshotCmd, RMT_CMD_SIZE, wait);

    if (rslt != ESP_OK) {
        DEBUG_PRINTE("RMT write failed: %s", esp_err_to_name(rslt));
    }
}

void DshotSetData(uint16_t data){
	for (int i = 0; i < 16; i++, data <<= 1)
	{
		if (data & 0x8000) // Set for each data the command information: from left to right bit shifting
		{
      // set zero
			_dshotCmd[i].duration0 = dt_t0h;
			_dshotCmd[i].level0 = 1;
			_dshotCmd[i].duration1 = dt_t0l;
			_dshotCmd[i].level1 = 0;

		}
		else
		{
      // set one
			_dshotCmd[i].duration0 = dt_t1h;
			_dshotCmd[i].level0 = 1;
			_dshotCmd[i].duration1 = dt_t1l;
			_dshotCmd[i].level1 = 0;
		}
	}
}

uint8_t DshotChecksum(uint16_t data){

    // Calculate checksum
	uint16_t csum = 0;
    
	for (int i = 0; i < 3; i++){
		csum ^= data;
		data >>= 4;
	}

	return csum & 0xf;
}


void DshotWritePacket(uint8_t channel, dshot_packet_t packet, bool wait){
    uint16_t data;

    // Assemble checksum
    data = packet.payload;
    data <<= 1;
    data |= packet.telemetry;
    data = (data << 4) | DshotChecksum(data);

    // Send data to RMT
	DshotWriteData(channel, data, wait);
}

void DshotSendThrottle(uint8_t channel, uint16_t ithrust)
{
    dshot_packet_t packet;

    if (ithrust > DSHOT_THROTTLE_MAX)
    {
        ithrust = DSHOT_THROTTLE_MAX;
    }
    else if (ithrust < DSHOT_THROTTLE_MIN)
    {
        ithrust = DSHOT_THROTTLE_MIN;
    }

    packet.payload = ithrust;
    packet.telemetry = 0;

    motor_ratios[channel] = (uint32_t)ithrust;
    DshotWritePacket(channel, packet, false);
}

void DshotRepeatPacket(uint8_t channel, dshot_packet_t packet, int n){

    for (int i = 0; i < n; i++)	{
        DshotWritePacket(channel, packet, true);
    }
}

void DshotSetReversed(bool reversed)
{ 
    dshot_packet_t packet[NBR_OF_MOTORS];
    for (int channel = 0; channel<NBR_OF_MOTORS; channel++){
        packet[channel].payload = reversed ? (uint16_t)SPIN_DIRECTION_REVERSED : (uint16_t)SPIN_DIRECTION_NORMAL;
        packet[channel].telemetry = 1;
        DshotRepeatPacket(channel, packet[channel], 10);
    }
    
}


LOG_GROUP_START(pwm)
LOG_ADD(LOG_UINT32, MOTOR_M1, &motor_ratios[0])
LOG_ADD(LOG_UINT32, MOTOR_M2, &motor_ratios[1])
LOG_ADD(LOG_UINT32, MOTOR_M3, &motor_ratios[2])
LOG_ADD(LOG_UINT32, MOTOR_M4, &motor_ratios[3])
LOG_GROUP_STOP(pwm)
