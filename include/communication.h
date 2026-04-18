#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "ff.h"

#define I2C_ADDR 0x60
#define I2C1_SDA 26 //define the SDA and SCL pins
#define I2C1_SCL 27




//425kHz for 22k files or 12k
//8192 samples per block I think lower sounds better tho?

//305 kHz for 16k files

#define I2C_BAUDRATE 305 * 1000
#define PWM_SLICE 0
#define SAMPLES_PER_BLOCK 256
#define I2C_CMDS_PER_BLOCK (SAMPLES_PER_BLOCK * 2)


extern uint16_t dma_buffer0[I2C_CMDS_PER_BLOCK];
extern uint16_t dma_buffer1[I2C_CMDS_PER_BLOCK];

extern int dma_chan0;
extern int dma_chan1;

extern volatile bool refill_buffer0;
extern volatile bool refill_buffer1;

void start_i2c_dma(void);

void format_audio_for_i2c(int16_t *audio_data, uint16_t *target_dma_buf);

void dma_irq_handler(void);

void SD_card_init(void);

void play_track(void);

//void next_track(void);

void read_audio(int16_t *audio_data, UINT bytes_to_read);
