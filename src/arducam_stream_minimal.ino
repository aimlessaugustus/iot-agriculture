// Minimal ArduCAM OV2640 streaming sketch
// Streams single JPEG frames over Serial continuously.

#include <Wire.h>
#include <SPI.h>
#include <ArduCAM.h>
#include "memorysaver.h"

#if !(defined OV2640_MINI_2MP)
#error Please enable OV2640_MINI_2MP in memorysaver.h
#endif

const int CS = 7;
ArduCAM myCAM(OV2640, CS);
const uint32_t MAX_FIFO = 0x7FFFF;

void setup() {
  Wire.begin();
  Serial.begin(921600);
  pinMode(CS, OUTPUT); digitalWrite(CS, HIGH);
  SPI.begin();

  myCAM.write_reg(ARDUCHIP_TEST1, 0x55);
  if (myCAM.read_reg(ARDUCHIP_TEST1) != 0x55) {
    Serial.println("ArduCAM SPI error");
    while (1) delay(1000);
  }

  myCAM.set_format(JPEG);
  myCAM.InitCAM();
  myCAM.OV2640_set_JPEG_size(OV2640_320x240);
  myCAM.clear_fifo_flag();
  Serial.println("ArduCAM ready - streaming frames to Serial");
}

void loop() {
  myCAM.flush_fifo();
  myCAM.clear_fifo_flag();
  myCAM.start_capture();

  unsigned long t0 = millis();
  while (!myCAM.get_bit(ARDUCHIP_TRIG, CAP_DONE_MASK)) {
    if (millis() - t0 > 2000) break; // capture timeout
  }

  uint32_t len = myCAM.read_fifo_length();
  if (len == 0 || len >= MAX_FIFO) { myCAM.clear_fifo_flag(); delay(100); return; }

  myCAM.CS_LOW();
  myCAM.set_fifo_burst();

  uint8_t prev = 0, cur = 0;
  cur = SPI.transfer(0x00); // prime
  len--;
  bool started = false;
  while (len--) {
    prev = cur; cur = SPI.transfer(0x00);
    if (started) Serial.write(cur);
    else if (prev == 0xFF && cur == 0xD8) { // SOI
      started = true; Serial.write(0xFF); Serial.write(0xD8);
    }
    if (started && prev == 0xFF && cur == 0xD9) break; // EOI
  }

  myCAM.CS_HIGH();
  myCAM.clear_fifo_flag();
  delay(100); // small pause between frames
}
