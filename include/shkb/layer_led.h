#pragma once

#include <stdint.h>

/* Light the local LED in the given layer's color (implemented in src/layer_led.c). */
void shkb_layer_led_show(uint8_t layer);
