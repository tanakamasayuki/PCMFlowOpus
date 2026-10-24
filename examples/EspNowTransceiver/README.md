# EspNowTransceiver

> 日本語版: [README.ja.md](README.ja.md)

Exchange encoded voice between ESP-NOW peers.

## Prepare and run

1. Check the [example guide](../../docs/GUIDE_EXAMPLES.md) for setup and hardware. Use the root README for supported boards and dependencies.
2. Open [EspNowTransceiver.ino](EspNowTransceiver.ino) and select the actual board/port. Prepare the dependencies used by these includes:

   `<M5Unified.h>`, `<PCMFlow.h>`, `<PCMFlowDeviceM5.h>`, `<PCMFlowOpus.h>`, `<esp_now.h>`, `<esp_wifi.h>`, `<WiFi.h>`

3. Upload and open the Serial monitor at 115200 baud. Check startup messages and input conditions in the source.

## Follow the sketch

PCMFlowOpus example: EspNowTransceiver

A half-duplex voice transceiver over ESP-NOW. Flash two M5Unified-
compatible boards (M5Stack Core2 / M5AtomEcho / M5StickC Plus / …)
with the same firmware. Holding button A on board A:

  M5.Mic (16 kHz mono int16, 20 ms frames)
    -> OpusEncoder.encodeFrame()  (24 kbps Opus packet, ~60 byte)
    -> esp_now_send(broadcast)

Whenever a packet arrives on board B (regardless of button state):

  esp_now_recv_cb           (queues packet bytes)
    -> OpusDecoder.decodePacket(pcm=nullptr)   (queues 320 PCM samples)
    -> PCMFlow.setInputSource(dec) + pump()    (rate / format / ring buffer)
    -> readFrames -> M5SpeakerBufferedPlayer

Bitrate / frame size:
  16 kHz mono, 20 ms -> ~60 byte/packet @ 24 kbps. Each ESP-NOW frame
  carries one Opus packet; the maximum ESP-NOW payload is 250 byte,
  so we are comfortably below the limit and could broadcast to many
  peers if desired.

Requires:
  - PCMFlow >= 0.2.1     (for streaming-source `setInputSource()` fix)
  - PCMFlowOpus          (this library)
  - M5Unified            (any board with M5.Mic + M5.Speaker)

These APIs are listed in their first source order. Registering a callback does not execute its body at that point.

1. `mic.begin(...)` — Initialize this component after its prerequisites; inspect the returned status where provided.
2. `audio.setInputSource(...)` — Attach a source that remains alive while the pipeline reads from it.
3. `audio.setOutputFormat(...)` — Choose a format supported by the next stage, not just the source format.
4. `audio.setBufferFrames(...)` — Size buffering in frames and account for both memory and added latency.
5. `mic.readFrames(...)` — Use the actual returned frame count; bytes also depend on channel count and sample width.
6. `enc.encodeFrame(...)` — Encode one supported frame and use the actual encoded length.
7. `dec.decodePacket(...)` — Decode the actual packet length and inspect errors or available output.
8. `audio.pump()` — Advance decoding/conversion, then consume only the available returned PCM frames.
9. `player.writeFrames(...)` — Check the number accepted and retain any unconsumed tail; a frame is not a byte.

Review these application constants before adapting the sketch: `kRate`, `kChannels`, `kFrame`, `kMaxPlayFrames`, `kBitrate`, `kRxRingSlots`, `kRxRingPktMax`. Match pins, channels and capacities to your application.

## API usage and precautions

The examples use M5Unified microphone and speaker hardware, PCMFlow and PCMFlowDevice as separate stages. Agree on sample rate, channels and frame duration; compressed packet length can vary. Enabling encoder FEC does not automatically make a receiver recover missing packets.

Check codec begin/encode/decode results and distinguish PCM frame counts from encoded byte counts. For a transceiver, use two boards on the same ESP-NOW channel and configure the peer/broadcast path used in the source. Hold the indicated button to transmit where the sketch uses push-to-talk.

Capturing audio, encoding, radio delivery, decoding and buffered playback can fail separately; inspect each stage instead of treating send success as audible output. Keep Wi-Fi receive callbacks short and copy/queue packet bytes for later processing. A loopback needs only one board and tests the codec path without testing radio delivery.

## Inputs and observable output

Button conditions used by this sketch: `BtnA.isPressed()`. Distinguish press edges, release edges and a continuously held button.

These are selected output formats from the source. printf specifiers are replaced by runtime values; inspect the surrounding conditions rather than assuming these lines always occur in this order.

```text
Ready. MAC=%s
```

## Verify and troubleshoot

Exchange encoded voice between ESP-NOW peers and verify the result on its output. First use the supplied input, then change one configuration or data source at a time.

| Symptom | First check |
|---|---|
| No output | Board/dependencies, initialization results, output destination and Serial baud |
| Starts but stops making progress | Wait conditions, buffer space, peer/input readiness and service calls |
| Unexpected values | Lengths/units, format, data lifetime, shared state and partial results |

See the [guide](../../docs/GUIDE_EXAMPLES.md) for shared principles and the [example index](../README.md) for the next sketch.
