// Encode → decode roundtrip tests.
//
// SCAFFOLDING: the codec is not yet implemented. Real tests will
// encode a generated 440 Hz sine wave with OpusEncoder, decode with
// OpusDecoder, and assert RMS error / PLC / FEC behavior.

#include <PCMFlowOpus.h>

void setup()
{
    Serial.begin(115200);
    delay(500);
    Serial.println("TEST start");

    // TODO: tests pending OpusEncoder/OpusDecoder implementation.

    Serial.println("TEST done 0/0");
}

void loop()
{
    delay(1);
}
