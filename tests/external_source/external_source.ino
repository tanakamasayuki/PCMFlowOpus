// PCMFlow::setInputSource(OpusDecoder) integration test.
//
// SCAFFOLDING: the decoder is not yet implemented. Real test will
// plug an OpusDecoder into a PCMFlow instance, feed Opus packets,
// and verify the PCMFlow pipeline produces the expected PCM via
// readFrames(), exactly mirroring the parent PCMFlow external_source
// harness.

#include <PCMFlow.h>
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
