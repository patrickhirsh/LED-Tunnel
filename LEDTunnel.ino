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
#define NUM_STRIP_LEDS 150

// LED Brightness (0-255)
#define BRIGHTNESS 255


// If 1, expects strips to be connected in a Zig-Zag pattern
// If 0, expects strips to be connected from the same side for each strip
#define ZIG_ZAG 1

// time (in ticks) to spend on each sequence
#define SEQUENCE_DURATION 300

// How many ticks ahead of the end of the sequence should we notify the sequence that it should prepare for a sequence transition
// This is mostly used for things like preventing ring chase from pulsing immediately before we transition, to make it smoother
#define WRAPUP_WARNING_OFFSET 20


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
const int S_RING_CHASE_PULSE_FREQUENCY = 45;

// pulse brightness falloff intensity (how fast does the pulse disappear?); larger == faster falloff
const int S_RING_CHASE_FALLOF_RATE = 8;

// ring shift frequency (frequency in ticks at which the pulse is echoed down the tunnel); larger == more delay (longer "echo")
const int S_RING_CHASE_RING_SHIFT_FREQUENCY = 2;


// -------------------- TWINKLE SEQUENCE CONFIG -------------------- //

// number of pulse colors to use
const int S_TWINKLE_NUM_COLORS = 1;

// pulse colors to cycle through during ring chase
const CHSV S_TWINKLE_COLORS[S_RING_CHASE_NUM_PULSE_COLORS] = { 
    CHSV(190, 255, 255),
};

// frequency at which twinkles occur; larger == less frequent
const int S_TWINKLE_FREQUENCY = 2;

// twinkle brightness falloff intensity (how fast does the twinkle disappear?); larger == faster falloff
const int S_TWINKLE_FALLOFF_RATE = 10;

// twinkle brightness variance; larger == more deviance from max brightness (0-255)
const int S_TWINKLE_BRIGHTNESS_VARIANCE = 100;


// -------------------- TRACE CHASE SEQUENCE CONFIG -------------------- //

// number of trace colors to use
const int S_TRACE_CHASE_NUM_COLORS = 3;

// pulse colors to cycle through during trace chase
const CHSV S_TRACE_CHASE_COLORS[S_TRACE_CHASE_NUM_COLORS] = { 
    CHSV(190, 255, 255), 
    CHSV(30, 255, 255), 
    CHSV(96, 255, 255) 
};

// trace brightness falloff intensity (how fast do the traces disappear?); larger == faster falloff
const int S_TRACE_FALLOFF_RATE = 25;

// how many leds per tick should the trace advance? larger number == faster.
const int S_TRACE_SPEED = 3;

// how many LEDs should each ring's trace be offset from the previous?
const int S_TRACE_OFFSET = 15;


// -------------------- Globals -------------------- //

// the actual FastLED led array
CRGB leds[NUM_STRIP_LEDS * NUM_STRIPS];

// get the index of the beginning of a strip by strip index
int GetStrip(int stripIndex) 
{ 
    return stripIndex * NUM_STRIP_LEDS; 
}


// -------------------- LED SEQUENCES -------------------- //

class LEDSequence
{
    public:
        virtual ~LEDSequence() = 0;
        virtual void Update();
        virtual void Init();
        virtual void WrapUp();
};

LEDSequence::~LEDSequence() {}


// ==================== S_RingChase ==================== //

class S_RingChase : public LEDSequence
{
    private:
        CHSV rings[NUM_STRIPS];

        int pulseTimer = 0;
        int pulseRingShiftTimer = 0;
        int pulseRingIndex = 0;
        int pulseColorIndex = S_RING_CHASE_NUM_PULSE_COLORS;

    public:   
        ~S_RingChase() {}

        S_RingChase()
        {
            for (int i = 0; i < NUM_STRIPS; i++)
            {
                rings[i] = S_RING_CHASE_COLORS[pulseColorIndex];
            }
        }

        void Init() 
        {
            pulseTimer = 0;
            pulseRingShiftTimer = 0;
            pulseRingIndex = 0;
            pulseColorIndex = S_RING_CHASE_NUM_PULSE_COLORS;
        }

        void WrapUp()
        {
            // prevent pulsing from happening right before a transition
            // lets just fade out...
            pulseTimer = 9999;
        }

        void Update()
        {
            // is it time to start another pulse chain?
            if (pulseTimer <= 0)
            {
                // Pulse!
                pulseTimer = S_RING_CHASE_PULSE_FREQUENCY;
                pulseRingIndex = 1;
                pulseRingShiftTimer = S_RING_CHASE_RING_SHIFT_FREQUENCY;
                pulseColorIndex = pulseColorIndex + 1 >= S_RING_CHASE_NUM_PULSE_COLORS ? 0 : pulseColorIndex + 1;
                rings[0] = S_RING_CHASE_COLORS[pulseColorIndex];
            }
            pulseTimer--;

            // are we actively pulsing down the tunnel?
            if (pulseRingShiftTimer <= 0 && pulseRingIndex < NUM_STRIPS) 
            {
                // pulse the next ring!
                rings[pulseRingIndex] = S_RING_CHASE_COLORS[pulseColorIndex];
                pulseRingShiftTimer = S_RING_CHASE_RING_SHIFT_FREQUENCY;
                pulseRingIndex++;
            }
            pulseRingShiftTimer--;

            // apply constant falloff on entire strip
            for (int i = 0; i < NUM_STRIPS; i++)
            {
                rings[i] = CHSV(
                    rings[i].h, 
                    rings[i].s, 
                    rings[i].v - S_RING_CHASE_FALLOF_RATE < 0 ? 0 : rings[i].v - S_RING_CHASE_FALLOF_RATE);
            }

            // set actual leds to ring states
            for (int i = 0; i < NUM_STRIPS; i++)
            {
                for (int j = 0; j < NUM_STRIP_LEDS; j++)
                {
                    leds[GetStrip(i) + j] = rings[i];
                }
            }
        }
};


// ==================== S_Twinkle ==================== //

class S_Twinkle : public LEDSequence
{
    private:
        int twinkleTimer = 0;

    public:   
        ~S_Twinkle() {}

        void Init() {}

        void WrapUp() {}

        void Update()
        {
            // is it time to twinkle?
            if (twinkleTimer <= 0)
            {
                // Twinkle!
                twinkleTimer = S_TWINKLE_FREQUENCY;
                int pos = random(1, NUM_STRIPS * NUM_STRIP_LEDS - 2);
                int brightness = random(255 - S_TWINKLE_BRIGHTNESS_VARIANCE, 255);
                CHSV color = S_TWINKLE_COLORS[random(S_TWINKLE_NUM_COLORS)];
                    leds[pos] = CHSV(color.h, color.s, brightness);
            }
            twinkleTimer--;

            // apply constant falloff on entire strip
            for (int i = 0; i < NUM_STRIPS *NUM_STRIP_LEDS; i++)
            {
                leds[i].nscale8(255 - S_TWINKLE_FALLOFF_RATE);
            }
        }
};


// ==================== S_TraceChase ==================== //

class S_TraceChase : public LEDSequence
{
    private:
        int traces[NUM_STRIPS];
        int traceColorIndex = 0;

    public:   
        ~S_TraceChase() {}

        void Init() 
        {
            traces[0] = 0;
#if ZIG_ZAG
            for (int i = 1; i < NUM_STRIPS; i++)
            {
                if (i % 2 == 1)
                {
                    traces[i] = NUM_STRIP_LEDS - i * S_TRACE_OFFSET;
                }
                else 
                {
                    traces[i] = i * S_TRACE_OFFSET;
                }
                
            }
#else
            for (int i = 1; i < NUM_STRIPS; i++)
            {
                traces[i] = i * S_TRACE_OFFSET;
            }
#endif
        }

        void WrapUp() {}

        void Update()
        {
            for (int i = 0; i < NUM_STRIPS; i++)
            {
#if ZIG_ZAG
                // odd strips should iterate backwards
                if (i % 2 == 1)
                {
                    for (int j = 0; j < S_TRACE_SPEED; j++)
                    {
                        traces[i]--;
                        if (traces[i] < 0)
                        {
                            traces[i] = NUM_STRIP_LEDS - 1;
                        }
                        
                        leds[GetStrip(i) + traces[i]] = S_TRACE_CHASE_COLORS[traceColorIndex];
                    }
                }
                else
                {
                    // advance strip i as many leds as S_TRACE_SPEED
                    for (int j = 0; j < S_TRACE_SPEED; j++)
                    {
                        traces[i]++;
                        if (traces[i] > NUM_STRIP_LEDS - 1)
                        {
                            traces[i] = 0;
                        }
                        
                        leds[GetStrip(i) + traces[i]] = S_TRACE_CHASE_COLORS[traceColorIndex];
                    }
                }    
#else
                // advance strip i as many leds as S_TRACE_SPEED
                for (int j = 0; j < S_TRACE_SPEED; j++)
                {
                    traces[i]++;
                    if (traces[i] > NUM_STRIP_LEDS - 1)
                    {
                        traces[i] = 0;
                    }
                    
                    leds[GetStrip(i) + traces[i]] = S_TRACE_CHASE_COLORS[traceColorIndex];
                }
#endif // ZIG_ZAG
                // only advance the trace color when we've finished updating this trace and are moving to the next strip
                traceColorIndex++;
                if (traceColorIndex > S_TRACE_CHASE_NUM_COLORS - 1)
                {
                    traceColorIndex = 0;
                }
            }

            // always apply the same color on the same strip (strip 0 is always color 0)
            traceColorIndex = 0; 

            // apply constant falloff on entire strip
            for (int i = 0; i < NUM_STRIPS *NUM_STRIP_LEDS; i++)
            {
                leds[i].nscale8(255 - S_TRACE_FALLOFF_RATE);
            }
        }
};


// -------------------- LED SEQUENCES -------------------- //

// LED Sequences to use
const int NUM_SEQUENCES = 3;
LEDSequence* Sequences[] = {
    new S_RingChase(),
    new S_Twinkle(),
    new S_TraceChase()
};

int sequenceTimer = SEQUENCE_DURATION;
int ledSequence = 0;

void fadeAll()
{
    // apply constant falloff on entire strip
    for (int i = 0; i < NUM_STRIPS *NUM_STRIP_LEDS; i++)
    {
        leds[i].nscale8(255 - S_TRACE_FALLOFF_RATE);
    }
}

// -------------------- STARTUP -------------------- //

void setup() 
{
    // tell FastLED about our LEDs
    FastLED.addLeds<WS2812B, LED_DATA_PIN, GRB>(leds, NUM_STRIP_LEDS * NUM_STRIPS);
    FastLED.setBrightness(BRIGHTNESS);

    // clear all LEDS of any lingering state
    FastLED.clear();   
    FastLED.show();

    Sequences[ledSequence]->Init();
}


// -------------------- MAIN LOOP -------------------- //

void loop() 
{
    sequenceTimer--;
    if (sequenceTimer == WRAPUP_WARNING_OFFSET)
    {
        Sequences[ledSequence]->WrapUp();
    }   

    if (sequenceTimer < 0)
    {
        ledSequence++;
        if (ledSequence > NUM_SEQUENCES - 1)
        {
            ledSequence = 0;
        }
        Sequences[ledSequence]->Init();
        sequenceTimer = SEQUENCE_DURATION;
    }

    Sequences[ledSequence]->Update();
    FastLED.show();
}
