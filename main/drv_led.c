/** 
    @file
    @brief   LED GPIO driver
    @author  borozdin.a
    @date    21.07.2020
*/

#include "driver/gpio.h"
#include "drv_led.h"

#include "config.h"

//------------ Definitions ----------//

typedef struct {
    uint16_t pin;
} ledPort_t;


//------------ Variables ------------//

// NET naming does not correspond to actual LED colors
static ledPort_t ledPins[LedCount] = {
    {TELEM_LED_PIN},
    {AAT_TELEM_MODE_LED_PIN},
    {AAT_CONFIG_MODE_LED_PIN}
};

//------------ Externals ------------//

//------------ Prototypes -----------//

//--------- Implementation ----------//


/**
    @brief  Init LED driver.
        GPIO clock must be enabled externally prior to calling this function
    @param  None
    @return None
*/
void drvLed_Init(void)
{
    uint8_t i;
    gpio_config_t io_conf = {};
    io_conf.intr_type = GPIO_INTR_DISABLE;
    io_conf.mode = GPIO_MODE_OUTPUT;
    io_conf.pull_down_en = 0;
    io_conf.pull_up_en = 0;
    for (i = 0; i < LedCount; i++)
    {
        io_conf.pin_bit_mask = BIT(ledPins[i].pin);
        gpio_config(&io_conf);
        gpio_set_level(ledPins[i].pin, !LEDS_ACTIVE_LEVEL);
    }
}


/**
    @brief  Set LED state
    @param[in]  led Led to set
    @param[in]  state New state, 1 = enable, 0 = disable
    @return None
*/
void drvLed_Set(Leds led, LedState state)
{
    if (led >= LedCount)
        return;
    gpio_set_level(ledPins[led].pin, (state) ? LEDS_ACTIVE_LEVEL : !LEDS_ACTIVE_LEVEL);
}
