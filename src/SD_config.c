#include "SD_config.h"
#include "sd_card.h"
#include "diskio.h"

spi_t spis[] = {
    {
        .hw_inst = spi0,  
        .miso_gpio = 16,
        .mosi_gpio = 19,
        .sck_gpio = 18,
        .baud_rate = 25000 * 1000,
        .set_drive_strength = true,
        .mosi_gpio_drive_strength = GPIO_DRIVE_STRENGTH_12MA,
        .sck_gpio_drive_strength = GPIO_DRIVE_STRENGTH_12MA
    }
};

static sd_card_t sd_cards[] = {
    {
        .pcName = "0:",           // The drive name we will mount
        .spi = &spis[0],          
        .ss_gpio = 17,            // Chip Select (CS) pin
        .use_card_detect = false, // The MSP2202 doesn't use this
        .card_detect_gpio = 0,    
        .card_detected_true = -1, 
        .m_Status = STA_NOINIT
    }
};

size_t sd_get_num() { return count_of(sd_cards); }
sd_card_t *sd_get_by_num(size_t num) {
    return (num <= sd_get_num()) ? &sd_cards[num] : NULL;
}
size_t spi_get_num() { return count_of(spis); }
spi_t *spi_get_by_num(size_t num) {
    return (num <= spi_get_num()) ? &spis[num] : NULL;
}

DWORD get_fattime(void) {
    return ((DWORD)(2024 - 1980) << 25)  // Year
         | ((DWORD)1 << 21)               // Month
         | ((DWORD)1 << 16);              // Day
}