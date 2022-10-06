// Written by Patrick Hirsh, October 2022

#include <bitswap.h>
#include <chipsets.h>
#include <color.h>
#include <colorpalettes.h>
#include <colorutils.h>
#include <controller.h>
#include <cpp_compat.h>
#include <dmx.h>
#include <FastLED.h>
#include <fastled_config.h>
#include <fastled_delay.h>
#include <fastled_progmem.h>
#include <fastpin.h>
#include <fastspi.h>
#include <fastspi_bitbang.h>
#include <fastspi_dma.h>
#include <fastspi_nop.h>
#include <fastspi_ref.h>
#include <fastspi_types.h>
#include <hsv2rgb.h>
#include <led_sysdefs.h>
#include <lib8tion.h>
#include <noise.h>
#include <pixelset.h>
#include <pixeltypes.h>
#include <platforms.h>
#include <power_mgt.h>
#include <math.h>


/* 
    LED-Tunnel

    Drives WS2812B leds with an Arduino.
    Designed for use with LEDs arranged in a series of arches, forming a tunnel.

    Usage:
        1. Specify the LED Data pin used on the arduino for controlling the LEDs.
        2. Specify the strip (arch) size and count for your tunnel. The sequences will scale automatically to these values
        3. Tweak the various sequences by changing the sequence variables in each sequence config section below (colors, animation speed, etc.)

    NOTE: All CHSV colors are specified by (Hue, Saturation, Value) in 0-255 value ranges.
    Keep this in mind when picking hue values, as hue is usually described on a scale of 0-360.
    See FastLED's docs here: https://github.com/FastLED/FastLED/wiki/FastLED-HSV-Colors
*/


// -------------------- CONFIG -------------------- //

// LED Data pin used on the Arduino
#define LED_DATA_PIN 6

// Number of strips (rings) in the light tunnel
#define NUM_STRIPS 8

// Number of LEDs per strip (ring) in the light tunnel
#define NUM_STRIP_LEDS 10


// -------------------- RING CHASE SEQUENCE CONFIG -------------------- //

// number of pulse colors to use
const int S_RING_CHASE_NUM_PULSE_COLORS = 3;

// pulse colors to cycle through during ring chase
const CHSV S_RING_CHASE_COLORS[S_RING_CHASE_NUM_PULSE_COLORS] = { 
    CHSV(190, 255, 255), 
    CHSV(30, 255, 255), 
    CHSV(96, 255, 255) 
};

// frequency at which new pulses occur; larger == less frequent
const int S_RING_CHASE_PULSE_FREQUENCY = 400;

// pulse brightness falloff intensity (how fast does the pulse disappear?); larger == faster falloff
const int S_RING_CHASE_FALLOF_RATE = 1;

// ring shift frequency (frequency in ticks at which the pulse is echoed down the tunnel); larger == more delay (longer "echo")
const int S_RING_CHASE_RING_SHIFT_FREQUENCY = 17;



// -------------------- Globals -------------------- //

// the actual FastLED led array
CRGB leds[NUM_STRIP_LEDS * NUM_STRIPS];

// HSV staging buffer before final write to FastLED's array
CHSV buffer[NUM_STRIP_LEDS * NUM_STRIPS];

// get the index of the beginning of a strip by strip index
int GetStrip(int stripIndex) { return stripIndex * NUM_STRIP_LEDS; }

// control globals
byte ledSequence = 0;           // stores which LED sequence we're currently in
byte ledBrightness = 100;       // global brightness value to scale all modes by


// -------------------- LED SEQUENCES -------------------- //

class LEDSequence
{
    public:
        virtual ~LEDSequence() = 0;
        virtual void Update();
};

LEDSequence::~LEDSequence() {}

// ==================== S_RingChase ==================== //

class S_RingChase : public LEDSequence
{
    private:
        int pulseTimer = 0;
        int pulseRingShiftTimer = 0;
        int pulseRingIndex = 0;
        int pulseColorIndex = 0;

    public:   
        ~S_RingChase() {}
        void Update()
        {
            // apply constant falloff on entire strip
            for (int i = 0; i < NUM_STRIP_LEDS * NUM_STRIPS; i++)
            {
                buffer[i] = CHSV(
                    buffer[i].h, 
                    buffer[i].s, 
                    buffer[i].v - S_RING_CHASE_FALLOF_RATE < 0 ? 0 : buffer[i].v - S_RING_CHASE_FALLOF_RATE);
            }

            // are we actively pulsing down the tunnel?
            if (pulseRingShiftTimer <= 0 && pulseRingIndex < NUM_STRIPS) 
            {
                // pulse the next ring!
                for (int i = 0; i < NUM_STRIP_LEDS; i++)
                {
                    buffer[GetStrip(pulseRingIndex) + i] = S_RING_CHASE_COLORS[pulseColorIndex];
                }
                pulseRingShiftTimer = S_RING_CHASE_RING_SHIFT_FREQUENCY;
                pulseRingIndex++;
            }
            pulseRingShiftTimer--;

            // is it time to start another pulse chain?
            if (pulseTimer <= 0)
            {
                // Pulse!
                for (int i = 0; i < NUM_STRIP_LEDS; i++)
                {
                    buffer[i] = S_RING_CHASE_COLORS[pulseColorIndex];
                }

                pulseTimer = S_RING_CHASE_PULSE_FREQUENCY;
                pulseRingIndex = 0;
                pulseRingShiftTimer = 0;
                pulseColorIndex = pulseColorIndex + 1 >= S_RING_CHASE_NUM_PULSE_COLORS ? 0 : pulseColorIndex + 1;
            }
            pulseTimer--;
        }
};


// -------------------- LED SEQUENCES -------------------- //

// LED Sequences to use
LEDSequence* Sequences[] = {
    new S_RingChase()
};


// -------------------- STARTUP -------------------- //

void setup() 
{
    // tell FastLED about our LEDs
    FastLED.addLeds<WS2812B, LED_DATA_PIN, GRB>(leds, NUM_STRIP_LEDS * NUM_STRIPS);

    FastLED.setBrightness(ledBrightness);

    // clear all LEDS of any lingering state
    FastLED.clear();   
    FastLED.show(); 
}


// -------------------- MAIN LOOP -------------------- //

void loop() 
{
    Sequences[ledSequence]->Update();

    // flush buffer to LED array
    for (int i = 0; i < NUM_STRIP_LEDS * NUM_STRIPS; i++)
    {
        leds[i] = buffer[i];
    }

    FastLED.show();
}

