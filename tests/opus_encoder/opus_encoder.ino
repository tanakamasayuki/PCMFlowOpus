// OpusEncoder unit tests.
//
// SCAFFOLDING: the encoder is not yet implemented, so this sketch only
// announces the test harness. Real assertions (EXPECT_TRUE / EXPECT_EQ /
// EXPECT_NEAR against ffmpeg-generated golden packets) will be added
// alongside the OpusEncoder implementation.

#include <PCMFlowOpus.h>

void setup()
{
    Serial.begin(115200);
    delay(500);
    Serial.println("TEST start");

    // TODO: tests pending OpusEncoder implementation.

    Serial.println("TEST done 0/0");
}

void loop()
{
    delay(1);
}
