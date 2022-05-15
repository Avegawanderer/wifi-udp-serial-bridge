#ifndef __DRV_LED_H__
#define __DRV_LED_H__

#define LEDS_ACTIVE_LEVEL       1

typedef enum {
    TelemLed,
    AatModeTelemLed,
    AatModeConfigLed,
    LedCount
} Leds;

typedef enum {
    Off,
    On
} LedState;


#ifdef __cplusplus
extern "C" {
#endif

    void drvLed_Init(void);
    void drvLed_Set(Leds led, LedState state);

#ifdef __cplusplus
}   // extern "C"
#endif

#endif // __DRV_LED_H__
