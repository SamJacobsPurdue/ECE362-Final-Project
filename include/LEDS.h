#pragma once

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include "pico/stdlib.h"
#include "hardware/pio.h"
#include "hardware/clocks.h"
#include "arm_math.h"
#include "hardware/adc.h"
#include "hardware/dma.h"

#define NUM_PIXELS 256
#define WS2812_PIN 13
#define ADC_PIN 41
#define ROWS 8
#define COLS 32
#define FFT_SIZE 64 //double the number of frequency bands you want

extern arm_rfft_fast_instance_f32 fft_instance;
extern float32_t adc_buffer[FFT_SIZE]; //incoming ADC data
extern float32_t fft_complex_output[FFT_SIZE]; //Temporary array for complex numbers
extern float32_t frequency_bins[FFT_SIZE / 2]; //usable FFT data
extern uint32_t ROW_COLORS[ROWS]; //pre loaded colors for each row
extern uint32_t INDEX_LUT[COLS][ROWS]; //lookup table of each index that is computed by init_index_lut
extern uint16_t raw_adc_buffer[FFT_SIZE]; //16 bit array for the RAW DMA data

void init_row_colors(uint8_t brightness);
void init_index_lut();
void fill_frame(uint32_t *frame, int *heights);
void send_frame(PIO pio, uint sm, uint32_t *frame);
void fft_init();
void process_audio_chunk();
void init_adc_dma();

static inline void put_pixel(PIO pio, uint sm, uint32_t pixel_grb) {
    pio_sm_put_blocking(pio, sm, pixel_grb << 8u); //shifts because our data is only 24 bits
}

static inline uint32_t set_color(uint8_t r, uint8_t g, uint8_t b, uint8_t brightness) {
    uint8_t dim_r = (r * brightness) >> 8; //divides each rgb value by 256 instantly for brightness setting
    uint8_t dim_g = (g * brightness) >> 8;
    uint8_t dim_b = (b * brightness) >> 8;
    return
            ((uint32_t) (dim_r) << 8) |
            ((uint32_t) (dim_g) << 16) |
            (uint32_t) (dim_b);
}