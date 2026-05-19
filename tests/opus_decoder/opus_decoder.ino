// OpusDecoder unit tests.
//
// SCAFFOLDING: the decoder is not yet implemented. Real tests will
// decode ffmpeg-generated Opus packets (440 Hz sine) and assert peak
// amplitude within ±10 % plus header consistency.

#include <PCMFlowOpus.h>

void setup()
{
    Serial.begin(115200);
    delay(500);
    Serial.println("TEST start");

    // TODO: tests pending OpusDecoder implementation.

    Serial.println("TEST done 0/0");
}

void loop()
{
    delay(1);
}
