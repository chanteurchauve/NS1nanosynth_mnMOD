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
// Calculated at MOZZI_CONTROL_RATE = 256Hz
// Steps = Target_ms * (256 / 1000)
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
  0, 69, 137, 206, 274, 343, 411, 480, 548, 617, 685, 754,                 
  822, 891, 960, 1028, 1097, 1165, 1234, 1302, 1371, 1439, 1508, 1576,      
  1645, 1713, 1782, 1850, 1919, 1988, 2056, 2125, 2193, 2262, 2330, 2399,   
  2467, 2536, 2604, 2673, 2741, 2810, 2878, 2947, 3016, 3084, 3153, 3221,   
  3290, 3358, 3427, 3495, 3564, 3632, 3701, 3769, 3838, 3907, 3975, 4044,   
  4095, 4095 
};

// --- MIDI SYNC AND LFO VARIABLES ---
byte clockCounter = 0;
unsigned long trigOffTime = 0;
bool trigActive = false;

const byte lfoTable[24] PROGMEM = {
  128, 161, 192, 219, 240, 252, 255, 250, 236, 213, 184, 151,
  118, 85, 54, 29, 11, 2, 0, 5, 18, 41, 70, 101
};

const byte lfoSubdivisions[8] PROGMEM = {24, 12, 48, 16, 96, 18, 36, 32}; 
byte lfoTickCounter = 0;

// --- CV INPUT CONFIGURATION (A3 -> A5) TO MIDI HOST ---
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

  // --- DIGITAL BUFFER RE-ENABLE FIX FOR ATMEGA32U4 ---
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
        // Legato Glide Back Fix: If notes are still held, glide to the newly exposed top note.
        if (noteCount > 0) {
          playTopNote(false, true); 
        } else {
          // No notes left, shut down cleanly.
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
      // If sustain drops and exposes a lower held note, glide to it.
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
          lfoTickCounter %= targetTicks;
        }

        byte lfoIndex = (lfoTickCounter * 24) / targetTicks;
        byte currentLfoVal = pgm_read_byte_near(&lfoTable[lfoIndex]);
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