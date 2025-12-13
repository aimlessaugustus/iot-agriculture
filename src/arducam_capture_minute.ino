// Capture a single small JPEG from an OV2640 every minute and write to Serial.
// SPI CS pin is 10

#include <Wire.h>
#include <SPI.h>
#include <ArduCAM.h>
#include "memorysaver.h"

#if !(defined OV2640_MINI_2MP)
#error Please enable OV2640_MINI_2MP in memorysaver.h
#endif

const int SPI_CS = 10;
ArduCAM myCAM(OV2640, SPI_CS);
const uint32_t MAX_FIFO = 0x7FFFF;
const unsigned long CAPTURE_INTERVAL = 60000; // 60 seconds

void setup() {
  Wire.begin();
  Serial.begin(921600);
  pinMode(SPI_CS, OUTPUT); digitalWrite(SPI_CS, HIGH);
  SPI.begin();

  // Reset controller
  myCAM.write_reg(0x07, 0x80); delay(100);
  myCAM.write_reg(0x07, 0x00); delay(100);

  // SPI sanity check
  myCAM.write_reg(ARDUCHIP_TEST1, 0x55);
  if (myCAM.read_reg(ARDUCHIP_TEST1) != 0x55) {
    Serial.println("ArduCAM SPI error");
    while (1) delay(1000);
  }

  // Configure camera
  myCAM.set_format(JPEG);
  myCAM.InitCAM();
  myCAM.OV2640_set_JPEG_size(OV2640_160x120); // small preview
  myCAM.clear_fifo_flag();

  Serial.println("ArduCAM capture-minute ready");
}

static unsigned long lastCapture = 0;

void captureOnce() {
  // Prepare and trigger capture
  myCAM.flush_fifo();
  myCAM.clear_fifo_flag();
  myCAM.start_capture();

  unsigned long t0 = millis();
  while (!myCAM.get_bit(ARDUCHIP_TRIG, CAP_DONE_MASK)) {
    if (millis() - t0 > 2000) break; // timeout
  }

  uint32_t len = myCAM.read_fifo_length();
  if (len == 0 || len >= MAX_FIFO) { myCAM.clear_fifo_flag(); Serial.println("Capture failed or too large"); return; }

  // Announce capture start (text). Serial listeners should detect SOI (0xFFD8).
  Serial.print("CAP_START "); Serial.println(len);

  // Burst read and stream JPEG bytes
  myCAM.CS_LOW();
  myCAM.set_fifo_burst();
  uint8_t prev = 0, cur = 0;
  cur = SPI.transfer(0x00); // prime
  if (len) len--;
  bool started = false;
  while (len--) {
    prev = cur; cur = SPI.transfer(0x00);
    if (started) {
      Serial.write(cur);
    } else if (prev == 0xFF && cur == 0xD8) {
      // SOI
      started = true;
      Serial.write((uint8_t)0xFF); Serial.write((uint8_t)0xD8);
    }
    if (started && prev == 0xFF && cur == 0xD9) break; // EOI
    delayMicroseconds(15);
  }
  myCAM.CS_HIGH();
  myCAM.clear_fifo_flag();

  Serial.println();
  Serial.println("CAP_DONE");
}

void loop() {
  unsigned long now = millis();
  if (now - lastCapture >= CAPTURE_INTERVAL) {
    lastCapture = now;
    captureOnce();
  }
  delay(100);
}
