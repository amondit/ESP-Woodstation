#include "HT1632.h"

/*
 * MID LEVEL FUNCTIONS
 * Functions that handle internal memory, initialize the hardware
 * and perform the rendering go here:
 */

void HT1632Class::begin(uint8_t pinCS1, uint8_t pinWR, uint8_t pinDATA) {
  _numActivePins = 1;
  _pinCS[0] = pinCS1;
  initialize(pinWR, pinDATA);
}

void HT1632Class::initialize(uint8_t pinWR, uint8_t pinDATA) {
  _pinWR = pinWR;
  _pinDATA = pinDATA;
  
  for (uint8_t i = 0; i < _numActivePins; ++i){
    pinMode(_pinCS[i], OUTPUT);
  }

  pinMode(_pinWR, OUTPUT);
  pinMode(_pinDATA, OUTPUT);
  
  select();
  
  for (uint8_t i = 0; i < NUM_CHANNEL; ++i) {
    // Allocate new memory for each channel
    mem[i] = (byte *)malloc(ADDR_SPACE_SIZE);
  }
  // Clear all memory
  clear();

  // Send configuration to chip:
  // This configuration is from the HT1632 datasheet, with one modification:
  //   The RC_MASTER_MODE command is not sent to the master. Since acting as
  //   the RC Master is the default behaviour, this is not needed. Sending
  //   this command causes problems in HT1632C (note the C at the end) chips. 
  
  // Send Master commands
  
  select(0b1111); // Assume that board 1 is the master.
  writeData(HT1632_ID_CMD, HT1632_ID_LEN);    // Command mode
  
  writeCommand(HT1632_CMD_SYSDIS); // Turn off system oscillator
  

  writeCommand(HT1632_CMD_COMS10);

  writeCommand(HT1632_CMD_SYSEN); //Turn on system
  writeCommand(HT1632_CMD_LEDON); // Turn on LED duty cycle generator
  writeCommand(HT1632_CMD_PWM(16)); // PWM 16/16 duty
  
  select();
  
  // Clear all screens by default:
  for(uint8_t i = 0; i < _numActivePins; ++i) {
    renderTarget(i);
    render();
  }
  // Set renderTarget to the first board.
  renderTarget(0);
}

void HT1632Class::selectChannel(uint8_t channel) {
  if(channel < NUM_CHANNEL) {
    _tgtChannel = channel;
  }
}

void HT1632Class::renderTarget(uint8_t target) {
  if(target < _numActivePins) {
    _tgtRender = target;
  }
}

void HT1632Class::setPixel(uint8_t x, uint8_t y) {
  if( x < 0 || x > OUT_SIZE || y < 0 || y > COM_SIZE )
    return;
  mem[_tgtChannel][GET_ADDR_FROM_X_Y(x, y)] |= (0b1 << PIXELS_PER_BYTE-1) >> (y % PIXELS_PER_BYTE);
}
void HT1632Class::clearPixel(uint8_t x, uint8_t y) {
  if( x < 0 || x > OUT_SIZE || y < 0 || y > COM_SIZE )
    return;
  mem[_tgtChannel][GET_ADDR_FROM_X_Y(x, y)] &= ~((0b1 << PIXELS_PER_BYTE-1) >> (y % PIXELS_PER_BYTE));
}
uint8_t HT1632Class::getPixel(uint8_t x, uint8_t y) {
  if( x < 0 || x > OUT_SIZE || y < 0 || y > COM_SIZE )
    return 0;
  return mem[_tgtChannel][GET_ADDR_FROM_X_Y(x, y)] & (0b1 << PIXELS_PER_BYTE-1) >> (y % PIXELS_PER_BYTE);
}

void HT1632Class::setPixel(uint8_t x, uint8_t y, uint8_t channel) {
  if( x < 0 || x > OUT_SIZE || y < 0 || y > COM_SIZE )
    return;
  mem[channel][GET_ADDR_FROM_X_Y(x, y)] |= GET_BIT_FROM_Y(y);
}
void HT1632Class::clearPixel(uint8_t x, uint8_t y, uint8_t channel) {
  if( x < 0 || x > OUT_SIZE || y < 0 || y > COM_SIZE )
    return;
  mem[channel][GET_ADDR_FROM_X_Y(x, y)] &= ~(GET_BIT_FROM_Y(y));
}
uint8_t HT1632Class::getPixel(uint8_t x, uint8_t y, uint8_t channel) {
  if( x < 0 || x > OUT_SIZE || y < 0 || y > COM_SIZE )
    return 0;
  return mem[channel][GET_ADDR_FROM_X_Y(x, y)] & GET_BIT_FROM_Y(y);
}

void HT1632Class::fill() {
  for(uint8_t i = 0; i < ADDR_SPACE_SIZE; ++i) {
    mem[_tgtChannel][i] = 0xFF;
  }
}
void HT1632Class::fillAll() {
  for(uint8_t c = 0; c < NUM_CHANNEL; ++c) {
    for(uint8_t i = 0; i < ADDR_SPACE_SIZE; ++i) {
      mem[c][i] = 0xFF; // Needs to be redrawn
    }
  }
}

void HT1632Class::clear(){
  for(uint8_t c = 0; c < NUM_CHANNEL; ++c) {
    for(uint8_t i = 0; i < ADDR_SPACE_SIZE; ++i) {
      mem[c][i] = 0x00; // Needs to be redrawn
    }
  }
}

// Draw the contents of mem
void HT1632Class::render() {
  if(_tgtRender >= _numActivePins) {
    return;
  }
  
  select(0b0001 << _tgtRender); // Selecting the chip
  
  writeData(HT1632_ID_WR, HT1632_ID_LEN);
  writeData(0, HT1632_ADDR_LEN); // Selecting the memory address

  // Write the channels in order
  for(uint8_t c = 0; c < NUM_CHANNEL; ++c) {
    for(uint8_t i = 0; i < ADDR_SPACE_SIZE; ++i) {
      // Write the higher bits before the the lower bits.
      writeData(mem[c][i] >> HT1632_WORD_LEN, HT1632_WORD_LEN); // Write the data in reverse.
      writeData(mem[c][i], HT1632_WORD_LEN); // Write the data in reverse.
    }
  }

  select(); // Close the stream at the end
}

// Set the brightness to an integer level between 1 and 16 (inclusive).
// Uses the PWM feature to set the brightness.
void HT1632Class::setBrightness(char brightness, char selectionmask) {
  if(selectionmask == 0b00010000) {
    if(_tgtRender < _numActivePins) {
      selectionmask = 0b0001 << _tgtRender;
    } else {
      return;
    }
  }
  
  select(selectionmask); 
  writeData(HT1632_ID_CMD, HT1632_ID_LEN);    // Command mode
  writeCommand(HT1632_CMD_PWM(brightness));   // Set brightness
  select();
}


/*
 * LOWER LEVEL FUNCTIONS
 * Functions that directly talk to hardware go here:
 */
 
void HT1632Class::writeCommand(char data) {
  writeData(data, HT1632_CMD_LEN);
  writeSingleBit();
} 
// Integer write to display. Used to write commands/addresses.
// PRECONDITION: WR is LOW
void HT1632Class::writeData(byte data, uint8_t len) {
  for(int j = len - 1, t = 1 << (len - 1); j >= 0; --j, t >>= 1){
    // Set the DATA pin to the correct state
    digitalWrite(_pinDATA, ((data & t) == 0)?LOW:HIGH);
    NOP(); // Delay 
    // Raise the WR momentarily to allow the device to capture the data
    digitalWrite(_pinWR, HIGH);
    NOP(); // Delay
    // Lower it again, in preparation for the next cycle.
    digitalWrite(_pinWR, LOW);
  }
}

// Write single bit to display, used as padding between commands.
// PRECONDITION: WR is LOW
void HT1632Class::writeSingleBit() {
  // Set the DATA pin to the correct state
  digitalWrite(_pinDATA, LOW);
  NOP(); // Delay
  // Raise the WR momentarily to allow the device to capture the data
  digitalWrite(_pinWR, HIGH);
  NOP(); // Delay
  // Lower it again, in preparation for the next cycle.
  digitalWrite(_pinWR, LOW);
}

void HT1632Class::setCLK(uint8_t pinCLK) {
  _pinCLK = pinCLK;
  pinMode(_pinCLK, OUTPUT);
  digitalWrite(_pinCLK, LOW);
}

inline void HT1632Class::pulseCLK() {
  digitalWrite(_pinCLK, HIGH);
  NOP();
  digitalWrite(_pinCLK, LOW);
}

void HT1632Class::select(uint8_t mask) {
  for(uint8_t i=0, t=1; i<_numActivePins; ++i, t <<= 1){
    digitalWrite(_pinCS[i], (t & mask)?LOW:HIGH);
  }
}

void HT1632Class::select() {
  select(0);
}

HT1632Class HT1632;
