// --- MOZZI 2.0 CORE CONFIGURATION ---
#define MOZZI_AUDIO_CHANNELS MOZZI_STEREO
#define MOZZI_CONTROL_RATE 256 

#include <MIDIUSB.h>
#include <SPI.h>
#include <Wire.h>
#include <DAC_MCP49xx.h>
#include <avr/pgmspace.h>

// Mozzi 2.0 Libraries
#include <Mozzi.h>
#include <Oscil.h>
#include <tables/saw2048_int8.h>
#include <mozzi_midi.h> 

// --- HARDWARE PIN CONFIGURATION ---
#define SS_PIN 4       
#define LDAC_PIN -1    
#define GATE_PIN 5     
#define LFO_PIN 11       
#define TRIG_PIN 12      

// LFO Shape Pins (Connect to GND to select)
#define LFO_WAVE_PIN_0 0
#define LFO_WAVE_PIN_1 1

// LFO Fraction Matrix Pins
#define LFO_MOD1_PIN 6   
#define LFO_MOD2_PIN 7   
#define LFO_MOD3_PIN 8   

// Portamento (Glide) Matrix Pins
#define GLIDE_PIN_A0 A0
#define GLIDE_PIN_A1 A1
#define GLIDE_PIN_A2 A2

DAC_MCP49xx dac(DAC_MCP49xx::MCP4922, SS_PIN, LDAC_PIN);

// --- MOZZI DIGITAL OSCILLATORS ---
Oscil <2048, AUDIO_RATE> aSaw1(SAW2048_DATA); 
Oscil <2048, AUDIO_RATE> aSaw2(SAW2048_DATA); 

#define MIN_NOTE 36 
#define MAX_NOTE 97 

// --- PITCH BEND AND SUSTAIN ---
int pitchBendOffset = 0;   
bool sustainActive = false;

// --- PORTAMENTO (GLIDE) VARIABLES ---
const int glideStepsTable[8] = {
  0,     // 000: Off
  13,    // 001: Only A0 (50ms)
  26,    // 010: Only A1 (100ms)
  128,   // 011: A0+A1   (500ms)
  64,    // 100: Only A2 (250ms)
  128,   // 101: A0+A2   (500ms)
  128,   // 110: A1+A2   (500ms)
  256    // 111: A0+A1+A2 (1000ms)
};

int glideCounter = 0;
float currentPitchFloat = 0.0;
float targetPitchFloat = 0.0;
float pitchGlideStep = 0.0;

float currentDacFloat = 0.0;
float targetDacFloat = 0.0;
float dacGlideStep = 0.0;

// --- I2C DIGIPOT CONFIGURATION ---
const byte digipot_addr = 0x2C;                    
const byte addresses[4] PROGMEM = { 0x00, 0x10, 0x60, 0x70 }; 

byte targetPotValues[4] = {0, 0, 0, 0};
bool potDirty[4] = {false, false, false, false};

// --- OPTIMIZED MONOPHONIC LIFO BUFFER ---
#define MAX_NOTES 8        
byte noteBuffer[MAX_NOTES];
byte velBuffer[MAX_NOTES];
bool heldBuffer[MAX_NOTES]; 
byte noteCount = 0;         

// --- MIDI TO CV CONVERSION ARRAY (PROGMEM) ---
const uint16_t DacVal[] PROGMEM = {
  0, 69, 137, 206, 275, 343, 412, 481, 549, 618, 687, 755,
  824, 893, 961, 1030, 1099, 1167, 1236, 1305, 1373, 1442, 1511, 1579,
  1648, 1717, 1785, 1854, 1923, 1991, 2060, 2129, 2197, 2266, 2335, 2403,
  2472, 2540, 2609, 2678, 2746, 2815, 2884, 2952, 3021, 3090, 3158, 3227,
  3296, 3364, 3433, 3502, 3570, 3639, 3708, 3776, 3845, 3914, 3982, 4051,
  4095, 4095
};

// --- MIDI SYNC AND LFO VARIABLES ---
byte clockCounter = 0;
unsigned long trigOffTime = 0;
bool trigActive = false;

// 256-sample Sine table for high resolution LFO. 
const byte lfoSine256[256] PROGMEM = {
  128, 131, 134, 137, 140, 143, 146, 149, 152, 156, 159, 162, 165, 168, 171, 174,
  176, 179, 182, 185, 188, 191, 193, 196, 199, 201, 204, 206, 209, 211, 213, 216,
  218, 220, 222, 224, 226, 228, 230, 232, 234, 236, 237, 239, 240, 242, 243, 245,
  246, 247, 248, 249, 250, 251, 252, 252, 253, 254, 254, 254, 255, 255, 255, 255,
  255, 255, 255, 255, 254, 254, 254, 253, 252, 252, 251, 250, 249, 248, 247, 246,
  245, 243, 242, 240, 239, 237, 236, 234, 232, 230, 228, 226, 224, 222, 220, 218,
  216, 213, 211, 209, 206, 204, 201, 199, 196, 193, 191, 188, 185, 182, 179, 176,
  174, 171, 168, 165, 162, 159, 156, 152, 149, 146, 143, 140, 137, 134, 131, 128,
  124, 121, 118, 115, 112, 109, 106, 103, 100, 96,  93,  90,  87,  84,  81,  78,
  76,  73,  70,  67,  64,  61,  59,  56,  53,  51,  48,  46,  43,  41,  39,  36,
  34,  32,  30,  28,  26,  24,  22,  20,  18,  16,  15,  13,  12,  10,  9,   7,
  6,   5,   4,   3,   2,   1,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
  0,   0,   0,   0,   0,   0,   0,   0,   1,   2,   3,   4,   5,   6,   7,   9,
  10,  12,  13,  15,  16,  18,  20,  22,  24,  26,  28,  30,  32,  34,  36,  39,
  41,  43,  46,  48,  51,  53,  56,  59,  61,  64,  67,  70,  73,  76,  78,  81,
  84,  87,  90,  93,  96,  100, 103, 106, 109, 112, 115, 118, 121, 124, 128
};

const byte lfoSubdivisions[8] PROGMEM = {24, 12, 48, 16, 96, 18, 36, 32}; 
byte lfoTickCounter = 0;

// --- CV INPUT CONFIGURATION ---
constexpr byte cvPins[3] = {A3, A4, A5};
int filteredCV[3] = {0, 0, 0};
int lastSentRaw[3] = {-999, -999, -999}; 
const int CV_NOISE_THRESHOLD = 12; 

void setup() {
  dac.setGain(2);
  dac.outputA(0);
  dac.outputB(0);

  pinMode(GATE_PIN, OUTPUT);
  digitalWrite(GATE_PIN, LOW);
  
  pinMode(TRIG_PIN, OUTPUT);
  digitalWrite(TRIG_PIN, LOW);
  pinMode(LFO_PIN, OUTPUT);
  
  pinMode(LFO_WAVE_PIN_0, INPUT_PULLUP);
  pinMode(LFO_WAVE_PIN_1, INPUT_PULLUP);

  pinMode(LFO_MOD1_PIN, INPUT_PULLUP);
  pinMode(LFO_MOD2_PIN, INPUT_PULLUP);
  pinMode(LFO_MOD3_PIN, INPUT_PULLUP);

  pinMode(GLIDE_PIN_A0, INPUT_PULLUP);
  pinMode(GLIDE_PIN_A1, INPUT_PULLUP);
  pinMode(GLIDE_PIN_A2, INPUT_PULLUP);

  for (byte i = 0; i < 3; i++) {
    pinMode(cvPins[i], INPUT_PULLUP);
  }

  Wire.begin();
  Wire.setClock(100000); 

  startMozzi();

  #if defined(__AVR_ATmega32U4__)
    DIDR0 &= ~(_BV(ADC7D) | _BV(ADC6D) | _BV(ADC5D)); 
  #endif
}

void i2c_send(byte addr, byte reg, byte val) {
  Wire.beginTransmission(addr);
  Wire.write(reg);
  Wire.write(val);
  Wire.endTransmission();
}

void DigipotWrite(byte pot, byte val) {
  i2c_send(digipot_addr, 0x40, 0xff);
  i2c_send(digipot_addr, 0xA0, 0xff);
  byte targetAddr = pgm_read_byte_near(&addresses[pot]); 
  i2c_send(digipot_addr, targetAddr, val);  
}

void updatePitchAndDac() {
  int finalDacA = constrain((int)currentDacFloat + pitchBendOffset, 0, 4095);
  dac.outputA(finalDacA);

  float finalPitch = currentPitchFloat + ((float)pitchBendOffset / 137.0f * 2.0f);
  aSaw1.setFreq(mtof(finalPitch));
  aSaw2.setFreq(mtof(finalPitch - 24.0f));
}

int getGlideSteps() {
  byte a0_active = (digitalRead(GLIDE_PIN_A0) == LOW) ? 1 : 0;
  byte a1_active = (digitalRead(GLIDE_PIN_A1) == LOW) ? 1 : 0;
  byte a2_active = (digitalRead(GLIDE_PIN_A2) == LOW) ? 1 : 0;
  
  byte state = (a2_active << 2) | (a1_active << 1) | a0_active;
  return glideStepsTable[state];
}

// --- AUDIO/GATE/PITCH GENERATION LOGIC ---
void playTopNote(bool triggerEnvelope, bool enableGlide) {
  if (noteCount > 0) {
    byte pitch = noteBuffer[noteCount - 1];
    byte vel = velBuffer[noteCount - 1];

    targetDacFloat = pgm_read_word_near(&DacVal[pitch - MIN_NOTE]);
    targetPitchFloat = (float)pitch; 

    if (triggerEnvelope) {
      uint16_t dacB = map(vel, 0, 127, 0, 4095);
      dac.outputB(dacB);
      digitalWrite(GATE_PIN, HIGH);
    }

    int activeGlideSteps = getGlideSteps();

    if (enableGlide && activeGlideSteps > 0) {
      glideCounter = activeGlideSteps;
      dacGlideStep = (targetDacFloat - currentDacFloat) / (float)activeGlideSteps;
      pitchGlideStep = (targetPitchFloat - currentPitchFloat) / (float)activeGlideSteps;
    } else {
      glideCounter = 0;
      currentDacFloat = targetDacFloat;
      currentPitchFloat = targetPitchFloat;
      updatePitchAndDac();
    }
  } else {
    dac.outputB(0);                
    digitalWrite(GATE_PIN, LOW);  
  }
}

// --- LIFO BUFFER MANAGEMENT LOGIC ---
void addNote(byte pitch, byte vel) {
  int foundIndex = -1;
  for (byte i = 0; i < noteCount; i++) {
    if (noteBuffer[i] == pitch) { foundIndex = i; break; }
  }

  if (foundIndex != -1) {
    for (byte i = foundIndex; i < noteCount - 1; i++) {
      noteBuffer[i] = noteBuffer[i + 1];
      velBuffer[i] = velBuffer[i + 1];
      heldBuffer[i] = heldBuffer[i + 1];
    }
    noteBuffer[noteCount - 1] = pitch;
    velBuffer[noteCount - 1] = vel;
    heldBuffer[noteCount - 1] = true;
  } else {
    if (noteCount < MAX_NOTES) {
      noteBuffer[noteCount] = pitch;
      velBuffer[noteCount] = vel;
      heldBuffer[noteCount] = true;
      noteCount++;
    } else {
      for (byte i = 0; i < MAX_NOTES - 1; i++) {
        noteBuffer[i] = noteBuffer[i + 1];
        velBuffer[i] = velBuffer[i + 1];
        heldBuffer[i] = heldBuffer[i + 1];
      }
      noteBuffer[MAX_NOTES - 1] = pitch;
      velBuffer[MAX_NOTES - 1] = vel;
      heldBuffer[MAX_NOTES - 1] = true;
    }
  }
  
  bool isLegato = (noteCount > 1);
  if (isLegato) {
    playTopNote(false, true); 
  } else {
    playTopNote(true, false); 
  }
}

void removeNote(byte pitch) {
  int foundIndex = -1;
  for (byte i = 0; i < noteCount; i++) {
    if (noteBuffer[i] == pitch) { foundIndex = i; break; }
  }

  if (foundIndex != -1) {
    heldBuffer[foundIndex] = false; 

    if (!sustainActive) {
      bool wasTop = (foundIndex == noteCount - 1);
      for (byte i = foundIndex; i < noteCount - 1; i++) {
        noteBuffer[i] = noteBuffer[i + 1];
        velBuffer[i] = velBuffer[i + 1];
        heldBuffer[i] = heldBuffer[i + 1];
      }
      noteCount--;

      if (wasTop || noteCount == 0) {
        if (noteCount > 0) {
          playTopNote(false, true); 
        } else {
          playTopNote(false, false); 
        }
      }
    }
  }
}

void handleSustain(byte value) {
  if (value >= 64 && !sustainActive) {
    sustainActive = true;
  } 
  else if (value < 64 && sustainActive) {
    sustainActive = false;
    byte newCount = 0;
    for (byte i = 0; i < noteCount; i++) {
      if (heldBuffer[i] == true) {
        noteBuffer[newCount] = noteBuffer[i];
        velBuffer[newCount] = velBuffer[i];
        heldBuffer[newCount] = true;
        newCount++;
      }
    }
    bool topChanged = (newCount != noteCount);
    noteCount = newCount;
    if (topChanged) { 
      if (noteCount > 0) {
          playTopNote(false, true);
      } else {
          playTopNote(false, false); 
      }
    }
  }
}

// --- MOZZI CONTROL LOGIC ---
void updateControl() {
  bool anyMidiSent = false;

  if (glideCounter > 0) {
    currentDacFloat += dacGlideStep;
    currentPitchFloat += pitchGlideStep;
    updatePitchAndDac();
    glideCounter--;
  }

  for (byte i = 0; i < 3; i++) {
    int rawCV = mozziAnalogRead<10>(cvPins[i]);
    filteredCV[i] = ((filteredCV[i] * 3) + rawCV) >> 2;

    if (abs(filteredCV[i] - lastSentRaw[i]) >= CV_NOISE_THRESHOLD) {
      lastSentRaw[i] = filteredCV[i];
      
      int mappedCC = map(filteredCV[i], 20, 1003, 0, 127);
      byte currentCC = constrain(mappedCC, 0, 127);
      
      byte ccNumber = 102 + i; 
      midiEventPacket_t ccTx = {0x0B, 0xB0, ccNumber, currentCC}; 
      MidiUSB.sendMIDI(ccTx);
      anyMidiSent = true;
    }
  }

  if (anyMidiSent) {
    MidiUSB.flush();
  }
}

StereoOutput updateAudio() {
  return StereoOutput::from8Bit(aSaw1.next(), aSaw2.next());
}

void loop() {
  audioHook(); 

  midiEventPacket_t rx;

  do {
    rx = MidiUSB.read();

    if (rx.header != 0) {
      byte status = rx.byte1 & 0xF0; 
      
      if (rx.header == 0x0F && rx.byte1 == 0xF8) {
        clockCounter++;
        if (clockCounter >= 6) { 
          clockCounter = 0;
          digitalWrite(TRIG_PIN, HIGH);
          trigActive = true;
          trigOffTime = millis() + 15; 
        }

        byte mod1 = (digitalRead(LFO_MOD1_PIN) == LOW) ? 1 : 0; 
        byte mod2 = (digitalRead(LFO_MOD2_PIN) == LOW) ? 1 : 0; 
        byte mod3 = (digitalRead(LFO_MOD3_PIN) == LOW) ? 1 : 0; 
        
        byte lfoState = (mod1 << 2) | (mod2 << 1) | mod3;
        byte targetTicks = pgm_read_byte_near(&lfoSubdivisions[lfoState]);

        if (lfoTickCounter >= targetTicks) {
          lfoTickCounter = 0;
        }

        // --- HIGH RESOLUTION INDEX CALCULATION (0-255) ---
        // Synchronized with MIDI Clock for smooth transitions
        byte lfoIndex = ((uint16_t)lfoTickCounter * 256) / targetTicks;

        // --- LFO SHAPE SELECTION LOGIC ---
        byte p0 = digitalRead(LFO_WAVE_PIN_0); 
        byte p1 = digitalRead(LFO_WAVE_PIN_1); 
        byte waveState = (p1 << 1) | p0;       

        byte currentLfoVal;
        switch (waveState) {
          case 0: // 00: Both pins to GND -> Saw Inverted
            currentLfoVal = 255 - lfoIndex; 
            break;
          case 1: // 01: Pin 1 to GND -> Saw
            currentLfoVal = lfoIndex; 
            break;
          case 2: // 10: Pin 0 to GND -> Pulse 50%
            currentLfoVal = (lfoIndex < 128) ? 255 : 0; 
            break;
          case 3: // 11: No pins to GND -> Sine
          default:
            currentLfoVal = pgm_read_byte_near(&lfoSine256[lfoIndex]); 
            break;
        }
        
        analogWrite(LFO_PIN, currentLfoVal); 
        
        lfoTickCounter++;
      }
      
      else if (status == 0x90 && rx.byte3 > 0) {
        if (rx.byte2 >= MIN_NOTE && rx.byte2 <= MAX_NOTE) {
          addNote(rx.byte2, rx.byte3);
        }
      }
      else if (status == 0x80 || (status == 0x90 && rx.byte3 == 0)) {
        removeNote(rx.byte2);
      }
      else if (status == 0xE0) {
        int pbValue = (rx.byte3 << 7) | rx.byte2; 
        pitchBendOffset = map(pbValue, 0, 16383, -137, 137);
        if (noteCount > 0) {
          updatePitchAndDac(); 
        }
      }
      else if (status == 0xB0) {
        byte ccNumber = rx.byte2;
        byte ccValue = rx.byte3;
        
        if (ccNumber == 64) {
          handleSustain(ccValue);
        }
        else if (ccNumber >= 30 && ccNumber <= 33) {
          byte potIndex = ccNumber - 30;
          targetPotValues[potIndex] = ccValue << 1; 
          potDirty[potIndex] = true;
        }
      }
    }
  } while (rx.header != 0);

  if (trigActive && millis() >= trigOffTime) {
    digitalWrite(TRIG_PIN, LOW);
    trigActive = false;
  }

  for (byte i = 0; i < 4; i++) {
    if (potDirty[i]) {
      DigipotWrite(i, targetPotValues[i]);
      potDirty[i] = false; 
      delay(2); 
    }
  }
}