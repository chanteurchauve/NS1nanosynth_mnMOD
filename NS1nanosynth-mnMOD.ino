// --- MOZZI 2.0 CORE CONFIGURATION ---
#define MOZZI_AUDIO_CHANNELS MOZZI_STEREO
#define MOZZI_CONTROL_RATE 64

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
#define LFO_PIN 11       // PWM Output for analog LFO 
#define TRIG_PIN 12      // Digital Output for Clock to Trigger

// LFO Fraction Matrix Pins
#define LFO_MOD1_PIN 6   // Modifier 1
#define LFO_MOD2_PIN 7   // Modifier 2
#define LFO_MOD3_PIN 8   // Modifier 3

DAC_MCP49xx dac(DAC_MCP49xx::MCP4922, SS_PIN, LDAC_PIN);

// --- MOZZI DIGITAL OSCILLATORS ---
Oscil <2048, AUDIO_RATE> aSaw1(SAW2048_DATA); // Main Oscillator (Channel L -> D9)
Oscil <2048, AUDIO_RATE> aSaw2(SAW2048_DATA); // Sub-Oscillator -2 Octaves (Channel R -> D10)

#define MIN_NOTE 36 
#define MAX_NOTE 97 

// --- PITCH BEND AND SUSTAIN VARIABLES ---
int currentBaseDacA = 0;   
int pitchBendOffset = 0;   
bool sustainActive = false;

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

// 24-step LFO Table (PROGMEM)
const byte lfoTable[24] PROGMEM = {
  128, 161, 192, 219, 240, 252, 255, 250, 236, 213, 184, 151,
  118, 85, 54, 29, 11, 2, 0, 5, 18, 41, 70, 101
};

// MIDI Ticks for LFO (PROGMEM) - States: 0=1/4, 1=1/8, 2=2/4, 3=1/8 triplets, 4=4/4, 5=dotted 1/8, 6=dotted 1/4, 7=1/4 triplets
const byte lfoSubdivisions[8] PROGMEM = {24, 12, 48, 16, 96, 18, 36, 32}; 
byte lfoTickCounter = 0;

// --- CV INPUT CONFIGURATION (A1 -> A5) WITH DSP FILTERING ---
constexpr byte cvPins[5] = {A1, A2, A3, A4, A5};
int filteredCV[5] = {0, 0, 0, 0, 0};
int lastSentRaw[5] = {-999, -999, -999, -999, -999}; 
const int CV_NOISE_THRESHOLD = 12; // Deadband (Hysteresis) against jitter

void setup() {
  dac.setGain(2);
  dac.outputA(0);
  dac.outputB(0);

  pinMode(GATE_PIN, OUTPUT);
  digitalWrite(GATE_PIN, LOW);
  
  pinMode(TRIG_PIN, OUTPUT);
  digitalWrite(TRIG_PIN, LOW);
  pinMode(LFO_PIN, OUTPUT);
  
  // Enable internal pull-up resistors for the fraction matrix
  pinMode(LFO_MOD1_PIN, INPUT_PULLUP);
  pinMode(LFO_MOD2_PIN, INPUT_PULLUP);
  pinMode(LFO_MOD3_PIN, INPUT_PULLUP);

  // Anchor CV pins to 5V to eliminate "antenna" noise when unpatched
  for (byte i = 0; i < 5; i++) {
    pinMode(cvPins[i], INPUT_PULLUP);
  }

  Wire.begin();
  Wire.setClock(100000); 

  startMozzi();
}

// --- I2C SUPPORT FUNCTIONS (DIGIPOT) ---
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

// --- AUDIO/GATE/PITCH GENERATION LOGIC ---
void playTopNote() {
  if (noteCount > 0) {
    byte pitch = noteBuffer[noteCount - 1];
    byte vel = velBuffer[noteCount - 1];

    currentBaseDacA = pgm_read_word_near(&DacVal[pitch - MIN_NOTE]);
    int finalDacA = constrain(currentBaseDacA + pitchBendOffset, 0, 4095);
    dac.outputA(finalDacA);
    
    uint16_t dacB = map(vel, 0, 127, 0, 4095);
    dac.outputB(dacB);

    float floatingPitch = (float)pitch + ((float)pitchBendOffset / 137.0f * 2.0f);
    aSaw1.setFreq(mtof(floatingPitch));
    aSaw2.setFreq(mtof(floatingPitch - 24.0f)); 

    digitalWrite(GATE_PIN, HIGH);
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
  playTopNote();
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
        playTopNote();
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
    if (topChanged) { playTopNote(); }
  }
}

// --- MOZZI CONTROL LOGIC (Control Rate) ---
void updateControl() {
  bool anyMidiSent = false;

  for (byte i = 0; i < 5; i++) {
    int rawCV = mozziAnalogRead<10>(cvPins[i]);

    // EMA Low-Pass Filter optimized via bit-shift (75% old, 25% new)
    filteredCV[i] = ((filteredCV[i] * 3) + rawCV) >> 2;

    // Hysteresis: check for variation exceeding noise threshold
    if (abs(filteredCV[i] - lastSentRaw[i]) >= CV_NOISE_THRESHOLD) {
      lastSentRaw[i] = filteredCV[i];
      
      // Dynamic recalibration: map a restricted raw range (e.g., 20-1003)
      // to the full MIDI range (0-127).
      int mappedCC = map(filteredCV[i], 20, 1003, 0, 127);
      
      // Mathematically ensure values are clamped to 0-127 range
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

// --- MOZZI 2.0 AUDIO GENERATION ---
StereoOutput updateAudio() {
  return StereoOutput::from8Bit(aSaw1.next(), aSaw2.next());
}

// --- MAIN LOOP ---
void loop() {
  audioHook(); 

  midiEventPacket_t rx;

  do {
    rx = MidiUSB.read();

    if (rx.header != 0) {
      byte status = rx.byte1 & 0xF0; 
      
      // CLOCK AND LFO SYNC MANAGEMENT
      if (rx.header == 0x0F && rx.byte1 == 0xF8) {
        
        clockCounter++;
        if (clockCounter >= 6) { // 16th note trigger out
          clockCounter = 0;
          digitalWrite(TRIG_PIN, HIGH);
          trigActive = true;
          trigOffTime = millis() + 15; 
        }

        // Proportional Phase LFO System 
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
      
      // NOTE ON
      else if (status == 0x90 && rx.byte3 > 0) {
        if (rx.byte2 >= MIN_NOTE && rx.byte2 <= MAX_NOTE) {
          addNote(rx.byte2, rx.byte3);
        }
      }
      // NOTE OFF
      else if (status == 0x80 || (status == 0x90 && rx.byte3 == 0)) {
        removeNote(rx.byte2);
      }
      // PITCH BEND
      else if (status == 0xE0) {
        int pbValue = (rx.byte3 << 7) | rx.byte2; 
        pitchBendOffset = map(pbValue, 0, 16383, -137, 137);
        if (noteCount > 0) {
          int finalDacA = constrain(currentBaseDacA + pitchBendOffset, 0, 4095);
          dac.outputA(finalDacA);
          
          byte pitch = noteBuffer[noteCount - 1];
          float floatingPitch = (float)pitch + ((float)pitchBendOffset / 137.0f * 2.0f);
          aSaw1.setFreq(mtof(floatingPitch));
          aSaw2.setFreq(mtof(floatingPitch - 24.0f));
        }
      }
      // CONTROL CHANGE
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

  // TRIGGER OUT DEACTIVATION
  if (trigActive && millis() >= trigOffTime) {
    digitalWrite(TRIG_PIN, LOW);
    trigActive = false;
  }

  // DIGIPOT UPDATE
  for (byte i = 0; i < 4; i++) {
    if (potDirty[i]) {
      DigipotWrite(i, targetPotValues[i]);
      potDirty[i] = false; 
      delay(2); 
    }
  }
}