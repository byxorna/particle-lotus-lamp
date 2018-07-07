/*
* Project particle-lotus-lamp
* Description: lotus lamp LEDs
* Author: Gabe Conradi
* Date: idklol
*/

#include "Particle.h"
#include "FastLED.h"

FASTLED_USING_NAMESPACE;
//SYSTEM_THREAD(ENABLED);
//SYSTEM_MODE(SEMI_AUTOMATIC);
SYSTEM_MODE(MANUAL);

struct DeckSettings {
  uint8_t label;
  float crossfadePositionActive;
  uint8_t pattern;
  uint8_t palette;
  uint8_t animationIndex;
  CRGBPalette16 currentPalette; // current color palette
  unsigned long tPatternStart;  // time last pattern changed
  unsigned long tPaletteStart;  // time last palette changed
};

DeckSettings deckSettingsA;
DeckSettings deckSettingsB;
DeckSettings* deckSettingsAll[] = {&deckSettingsA, &deckSettingsB};

typedef void (*FP)(CRGB*, DeckSettings*);

// Use qsuba for smooth pixel colouring and qsubd for non-smooth pixel colouring
#define qsubd(x, b)  ((x>b)?b:0)                              // Digital unsigned subtraction macro. if result <0, then => 0. Otherwise, take on fixed value.
#define qsuba(x, b)  ((x>b)?x-b:0)                            // Analog Unsigned subtraction macro. if result <0, then => 0

#define MOM_NUM_LEDS 83
#define GABE_NUM_LEDS 83
#define NUM_LEDS 83
#define LEDS_PIN D6
#define LED_TYPE NEOPIXEL
#define UPDATES_PER_SECOND 240
#define MAX_BRIGHTNESS 255
#define MAX_SATURATION 255
#define BOOTUP_ANIM_DURATION_MS 5000
#define PATTERN_CHANGE_INTERVAL_MS 30000
#define PALETTE_CHANGE_INTERVAL_MS 30000
#define AUTO_CHANGE_PALETTE 1
#define AUTO_PATTERN_CHANGE true
#define GLOBAL_BRIGHTNESS 255

#define VJ_CROSSFADING_ENABLED 1
// make the crossfade take a long time to mix patterns up a lot and get interesting behavior
#define VJ_CROSSFADE_DURATION_MS 10000.0
#define VJ_NUM_DECKS 2
#define VJ_DECK_SWITCH_INTERVAL_MS 15000

/* crossfading global state */
CRGB masterOutput[NUM_LEDS];
CRGB deckA[NUM_LEDS];
CRGB deckB[NUM_LEDS];
float crossfadePosition = 1.0;  // 0.0 is deckA, 1.0 is deckB
int crossfadeDirection = (crossfadePosition == 1.0) ? -1 : 1; // start going B -> A
uint8_t crossfadeInProgress = 0;
unsigned long tLastCrossfade = 0;

unsigned long t_now;                // time now in each loop iteration
unsigned long t_boot;               // time at bootup

TBlendType currentBlending = LINEARBLEND;
CFastLED* gLED; // global CFastLED object

/* custom color palettes */
// orange 255,102,0 FF6600
// pink 255,0,255 #ff00ff
// pornj 255,51,51 #ff3333
DEFINE_GRADIENT_PALETTE( Disorient_gp ) {
      0,   0,   0,   0,    // black
     75, 255,  26, 153,    // pink
    147, 255,  51,  51,    // pornj
    208, 255, 111,  15,    // orange
    255, 255, 255, 255, }; // white

DEFINE_GRADIENT_PALETTE( es_pinksplash_08_gp ) {
    0, 126, 11,255,
  127, 197,  1, 22,
  175, 210,157,172,
  221, 157,  3,112,
  255, 157,  3,112};


// for effects that are palette based
CRGBPalette16 palettes[] = {
  Disorient_gp,
  CloudColors_p,
  ForestColors_p,
  es_pinksplash_08_gp,
  OceanColors_p,
  LavaColors_p,
};
#define PALETTES_COUNT (sizeof(palettes)/sizeof(*palettes))

void  pattern_plasma(CRGB* leds, DeckSettings* s) {                                                 // This is the heart of this program. Sure is short. . . and fast.

  int thisPhase = beatsin8(6,-64,64);                           // Setting phase change for a couple of waves.
  int thatPhase = beatsin8(7,-64,64);

  for (int k=0; k<NUM_LEDS; k++) {                              // For each of the LED's in the strand, set a brightness based on a wave as follows:

    int colorIndex = cubicwave8((k*23)+thisPhase)/2 + cos8((k*15)+thatPhase)/2;           // Create a wave and add a phase change and add another wave with its own phase change.. Hey, you can even change the frequencies if you wish.
    int thisBright = qsuba(colorIndex, beatsin8(7,0,96));                                 // qsub gives it a bit of 'black' dead space by setting sets a minimum value. If colorIndex < current value of beatsin8(), then bright = 0. Otherwise, bright = colorIndex..

    leds[k] = ColorFromPalette(s->currentPalette, colorIndex, thisBright, currentBlending);  // Let's now add the foreground colour.
  }
}

void pattern_slow_pulse(CRGB* leds, DeckSettings* s) {
  // pick a color, and pulse it 
  uint8_t cBrightness = beatsin8(20, 120, 255);
  uint8_t cHue = beatsin8(4, 0, 255);
  CHSV hsv_led = CHSV(cHue, 255, cBrightness);
  CRGB rgb_led;
  hsv2rgb_rainbow(hsv_led, rgb_led);
  for( int i = 0; i < NUM_LEDS; i++) {
    leds[i] = rgb_led;
  }
}

void pattern_cylon_eye(CRGB* leds, DeckSettings* s) {
  // cylon eye is 4 pixels wide, +/++ base index
  // we map a 60bpm(1s) cycle into 0..num leds-1
  uint8_t h = beatsin8(8, 0, 255);
  CHSV hsv_led = CHSV(h, 255, 255);
  CRGB rgb_led;
  hsv2rgb_rainbow(hsv_led, rgb_led);
  uint8_t mappedIndex = beatsin8(60, 0, NUM_LEDS-1);
  for(int i = 0; i < NUM_LEDS; ++i) {
    if (mappedIndex == i) {
      leds[i] = rgb_led;
    } else if (addmod8(mappedIndex, 1, 255) == i) {
      leds[i] = rgb_led;
    } else if (addmod8(mappedIndex, 2, 255) == i) {
      leds[i] = rgb_led;
    } else if (addmod8(mappedIndex, 3, 255) == i) {
      leds[i] = rgb_led;
    } else {
      leds[i] = CRGB::Black;
    }
  }
}

void pattern_bootup(CRGB* leds, DeckSettings* s) {
  // ramp intensity up as we boot up. act like we are warming up
  float intensity = (t_now/BOOTUP_ANIM_DURATION_MS) * 255.0;
  uint8_t v = 255;
  if (intensity >= 255.0) {
    v = 255;
  }
  Serial.printlnf("t=%d i=%d", t_now, v);
  uint8_t baseHue = beatsin8(3, 0, 255);
  uint8_t iHue = 0;
  for(int i = 0; i < NUM_LEDS; ++i) {
    iHue = addmod8(baseHue, 1, 255);
    CHSV hsv_led = CHSV(iHue, 255, v);
    CRGB rgb_led;
    hsv2rgb_rainbow(hsv_led, rgb_led);
    leds[i] = rgb_led;
  }
}

// cycle a rainbow, varying how quickly it rolls around the board
void pattern_rainbow_waves(CRGB* leds, DeckSettings* s) {
  for(int i = 0; i < NUM_LEDS; ++i) {
    uint8_t h = (t_now/12+i)%256;
    CHSV hsv_led = CHSV(h, 255, 255);
    CRGB rgb_led;
    hsv2rgb_rainbow(hsv_led, rgb_led);
    leds[i] = rgb_led;
  }
}

void pattern_clear(NSFastLED::CRGB* leds) {
  for( int i = 0; i < NUM_LEDS; i++) {
    leds[i] = CRGB::Black;
  }
}
void pattern_from_palette(CRGB* leds, DeckSettings* s) {
  uint8_t b = beatsin8(4, 0, 255);
  for( int i = 0; i < NUM_LEDS; i++) {
    leds[i] = ColorFromPalette(s->currentPalette, s->animationIndex + i + b, MAX_BRIGHTNESS, currentBlending);
  }
  // slow down progression by 1/3
  if (t_now%3 == 0) {
    s->animationIndex = addmod8(s->animationIndex, 1, 255);
  }
}

void pattern_brake_light(CRGB* leds, DeckSettings* s) {
  for (int i = 0; i < NUM_LEDS; ++i) {
    leds[i] = CRGB::Red;
  }
}

// NOTE: lifted and tweaked from https://learn.adafruit.com/rainbow-chakra-led-hoodie/the-code
// This function draws color waves with an ever-changing,
// widely-varying set of parameters, using a color palette.
void pattern_palette_waves(CRGB* leds, DeckSettings* s) {
  uint8_t numleds = NUM_LEDS;
  static uint16_t sPseudotime = 0;
  static uint16_t sLastMillis = 0;
  static uint16_t sHue16 = 0;

  //uint8_t sat8 = beatsin88( 87, 220, 250);
  uint8_t brightdepth = beatsin88( 341, 96, 224);
  uint16_t brightnessthetainc16 = beatsin88( 203, (25 * 256), (40 * 256));
  uint8_t msmultiplier = beatsin88(147, 23, 60);

  uint16_t hue16 = sHue16;//gHue * 256;
  uint16_t hueinc16 = beatsin88(113, 300, 1500);

  uint16_t ms = millis();
  uint16_t deltams = ms - sLastMillis ;
  sLastMillis  = ms;
  sPseudotime += deltams * msmultiplier;
  sHue16 += deltams * beatsin88( 400, 5,9);
  uint16_t brightnesstheta16 = sPseudotime;

  for( uint16_t i = 0 ; i < numleds; i++) {
    hue16 += hueinc16;
    uint8_t hue8 = hue16 / 256;
    uint16_t h16_128 = hue16 >> 7;
    if( h16_128 & 0x100) {
      hue8 = 255 - (h16_128 >> 1);
    } else {
      hue8 = h16_128 >> 1;
    }

    brightnesstheta16  += brightnessthetainc16;
    uint16_t b16 = sin16( brightnesstheta16  ) + 32768;

    uint16_t bri16 = (uint32_t)((uint32_t)b16 * (uint32_t)b16) / 65536;
    uint8_t bri8 = (uint32_t)(((uint32_t)bri16) * brightdepth) / 65536;
    bri8 += (255 - brightdepth);

    uint8_t index = hue8;
    index = scale8( index, 240);

    CRGB newcolor = ColorFromPalette(s->currentPalette, index, bri8);

    uint16_t pixelnumber = i;
    pixelnumber = (numleds-1) - pixelnumber;

    nblend(leds[pixelnumber], newcolor, 128);
  }
}

/** update this with patterns you want to be cycled through **/
#define NUM_PATTERNS sizeof(patternBank) / sizeof(FP)
const FP patternBank[] = {
  //&pattern_from_palette,
  &pattern_plasma,
  &pattern_slow_pulse,
  &pattern_palette_waves,
  &pattern_rainbow_waves,
};

void randomPattern(DeckSettings* deck, DeckSettings* otherDeck) {
  uint8_t old = deck->pattern;
  while (deck->pattern == old || deck->pattern == otherDeck->pattern) {
    deck->pattern = random8(0, NUM_PATTERNS);
  }
  deck->tPatternStart = t_now;
}

void randomPalette(DeckSettings* deck, DeckSettings* otherDeck) {
  uint8_t old = deck->palette;
  while (deck->palette == old || deck->palette == otherDeck->palette) {
    deck->palette = random8(0, PALETTES_COUNT);
  }
  deck->currentPalette = palettes[deck->palette];
  deck->tPaletteStart = t_now;
}

// setup() runs once, when the device is first turned on.
void setup() {

  Serial.begin(9600);
  Serial.println("resetting");

  t_now = millis();
  t_boot = t_now;
  tLastCrossfade = t_now;

  deckSettingsA = {
    1,
    0.0,
    0,
    0,
    0,
    palettes[0],
    t_now,
    t_now,
  };
  deckSettingsB = {
    2,
    1.0,
    0,
    0,
    0,
    palettes[0],
    t_now,
    t_now,
  };
  randomPattern(&deckSettingsA, &deckSettingsB);
  randomPalette(&deckSettingsA, &deckSettingsB);
  randomPattern(&deckSettingsB, &deckSettingsA);
  randomPalette(&deckSettingsB, &deckSettingsA);

  // led controller, data pin, clock pin, RGB type (RGB is already defined in particle)
  gLED = new CFastLED();
  gLED->addLeds<LED_TYPE, LEDS_PIN>(masterOutput, NUM_LEDS);
  gLED->setBrightness(GLOBAL_BRIGHTNESS);
  pattern_clear(masterOutput);
  pattern_clear(deckA);
  pattern_clear(deckB);
  gLED->show();

  Serial.println("booted up");
}



void loop() {
  t_now = millis();

  // increment pattern every PATTERN_CHANGE_INTERVAL_MS, but not when a deck is active!
  if (AUTO_PATTERN_CHANGE) {
    if (t_now > deckSettingsA.tPatternStart+PATTERN_CHANGE_INTERVAL_MS && !crossfadeInProgress) {
      if (crossfadePosition == 1.0) {
        randomPattern(&deckSettingsA, &deckSettingsB);
        Serial.printlnf("deckA.pattern=%d", deckSettingsA.pattern);
      }
    }
    if (t_now > deckSettingsB.tPatternStart+PATTERN_CHANGE_INTERVAL_MS && !crossfadeInProgress) {
      if (crossfadePosition == 0.0) {
        randomPattern(&deckSettingsB, &deckSettingsA);
        Serial.printlnf("deckB.pattern=%d", deckSettingsB.pattern);
      }
    }
  }

  // increment palette every PALETTE_CHANGE_INTERVAL_MS, but not when crossfading!
  if (AUTO_CHANGE_PALETTE && !crossfadeInProgress) {
    for (int x = 0; x < VJ_NUM_DECKS ; x++){
      int xOther = (x == 0) ? 1 : 0;
      DeckSettings* deck = deckSettingsAll[x];
      DeckSettings* otherdeck = deckSettingsAll[xOther];
      if ((deck->crossfadePositionActive != crossfadePosition) &&
          (deck->tPaletteStart + PALETTE_CHANGE_INTERVAL_MS < t_now)) {
        randomPalette(deck, otherdeck);
        Serial.printlnf("deck%d.palette=%d", deck->label, deck->palette);
      }
    }
  }

  if (t_boot + BOOTUP_ANIM_DURATION_MS > t_now) {
    // display a bootup pattern for a bit
    pattern_bootup(deckA, &deckSettingsA);
    for (int i = 0; i < NUM_LEDS; ++i) {
      deckB[i] = deckA[i];
    }
  } else {
    // fill in patterns on both decks! we will crossfade master output later
    // NOTE: only render to a deck if its "visible" through the crossfader
    if ( !VJ_CROSSFADING_ENABLED || crossfadePosition < 1.0 ) {
      patternBank[deckSettingsA.pattern](deckA, &deckSettingsA);
    }
    if ( VJ_CROSSFADING_ENABLED && crossfadePosition > 0 ) {
      patternBank[deckSettingsB.pattern](deckB, &deckSettingsB);
    }
  }

  // perform crossfading increment if we are mid pattern change
  if (VJ_CROSSFADING_ENABLED) {
    //Serial.printf("%d %d %d\n", t_now, tLastCrossfade + VJ_DECK_SWITCH_INTERVAL_MS, crossfadeInProgress);
    if (t_now > tLastCrossfade + VJ_DECK_SWITCH_INTERVAL_MS && !crossfadeInProgress) {
      // start switching between decks
      Serial.printf("starting fading to %c\n", (crossfadePosition == 1.0) ? 'A' : 'B');
      crossfadeInProgress = 1;
      tLastCrossfade = t_now;
    }
    if (crossfadeInProgress) {
      float step = (float)1.0/(VJ_CROSSFADE_DURATION_MS/1000.0*UPDATES_PER_SECOND);
      // Serial.printf("fader increment %f %d\n", step, crossfadeDirection);
      crossfadePosition = crossfadePosition + crossfadeDirection * step;

      // is it time to change decks?
      // we are cut over to deck B, break this loop
      if (crossfadePosition > 1.0) {
        crossfadePosition = 1.0;
        crossfadeDirection = -1; // 1->0
        crossfadeInProgress = 0;
        Serial.printf("finished fading to B\n");
      }
      // we are cut over to deck B
      if (crossfadePosition < 0.0) {
        crossfadePosition = 0.0;
        crossfadeDirection = 1;  // 0->1
        crossfadeInProgress = 0;
        Serial.printf("finished fading to A\n");
      }
    }
  }

  // perform crossfading between deckA and deckB, by filling masterOutput
  // FIXME for now, lets just take a linear interpolation between deck a and b
  for (int i = 0; i < NUM_LEDS; ++i) {
    if (VJ_CROSSFADING_ENABLED) {
      masterOutput[i] = deckA[i].lerp8(deckB[i], fract8(255*crossfadePosition));
    } else {
      masterOutput[i] = deckA[i];
    }
  }

  gLED->setBrightness(GLOBAL_BRIGHTNESS);
  gLED->show();
  delay(1000.0 / UPDATES_PER_SECOND);
}
