// EspNowTransceiver — placeholder sketch.
//
// Showcase sketch for the headline PCMFlowOpus use case:
//   mic  → OpusEncoder → ESP-NOW broadcast
//   ESP-NOW receive → OpusDecoder → PCMFlow → I2S DAC
//
// All in one binary, so two boards flashed with the same firmware can
// half-duplex talk to each other.
//
// STATUS: not implemented yet. Will be filled in once OpusEncoder /
// OpusDecoder are functional. The README references this directory as
// the headline example, so it is kept in the scaffolding even before
// the implementation exists.

#include <PCMFlow.h>
#include <PCMFlowOpus.h>

void setup()
{
    Serial.begin(115200);
    Serial.println("EspNowTransceiver — placeholder.");
    // TODO: implement once OpusEncoder/OpusDecoder are available.
}

void loop()
{
    delay(1);
}
