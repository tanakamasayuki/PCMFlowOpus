// en: Exchange encoded voice between ESP-NOW peers
// ja: ESP-NOWの相手と符号化した音声を交換する
#include <M5Unified.h>
#include <PCMFlow.h>
#include <PCMFlowDeviceM5.h>
#include <PCMFlowOpus.h>

#include <esp_now.h>
#include <esp_wifi.h>
#include <WiFi.h>

// ---- Audio format ---------------------------------------------------------
static constexpr uint32_t kRate     = 16000;
static constexpr uint8_t  kChannels = 1;
static constexpr size_t   kFrame    = 320;     // 20 ms @ 16 kHz
static constexpr size_t   kMaxPlayFrames = (kRate * 80u) / 1000u;
static constexpr int      kBitrate  = 24000;   // 24 kbps Opus
using Player = M5SpeakerBufferedPlayer<kMaxPlayFrames>;

// ---- ESP-NOW --------------------------------------------------------------
static const uint8_t kBroadcastMac[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

// Small lock-free-ish ring of recently-received Opus packets. Push in
// the ESP-NOW recv callback (runs in a Wi-Fi task), drain in loop().
static constexpr int kRxRingSlots = 8;
static constexpr int kRxRingPktMax = 400;
struct RxPacket {
    uint8_t  buf[kRxRingPktMax];
    uint16_t len = 0;
};
static volatile RxPacket  g_rxRing[kRxRingSlots];
static volatile uint8_t   g_rxHead = 0;   // producer
static volatile uint8_t   g_rxTail = 0;   // consumer

static void onEspNowRecv(const esp_now_recv_info_t * /*info*/,
                         const uint8_t *data, int len)
{
    if (len <= 0 || len > kRxRingPktMax) return;
    const uint8_t next = (g_rxHead + 1) % kRxRingSlots;
    if (next == g_rxTail) return;          // drop on overrun
    RxPacket *slot = const_cast<RxPacket *>(&g_rxRing[g_rxHead]);
    memcpy(slot->buf, data, len);
    slot->len = static_cast<uint16_t>(len);
    g_rxHead = next;
}

static bool espNowBegin()
{
    WiFi.mode(WIFI_STA);
    if (esp_now_init() != ESP_OK) return false;
    esp_now_register_recv_cb(onEspNowRecv);

    esp_now_peer_info_t peer = {};
    memcpy(peer.peer_addr, kBroadcastMac, 6);
    peer.channel = 0;
    peer.encrypt = false;
    esp_now_add_peer(&peer);
    return true;
}

// ---- Mic source: M5.Mic synchronous capture -------------------------------
class M5MicSource : public PCMSource
{
public:
    bool begin() {
        format_ = {kRate, kChannels, 16};
        return M5.Mic.isEnabled();
    }
    const PCMFormat &format() const override { return format_; }
    bool isEof() const override { return false; }
    bool isReady() const override { return M5.Mic.isEnabled(); }
    size_t readFrames(void *out, size_t frameCount) override {
        if (!M5.Mic.isEnabled() || out == nullptr || frameCount == 0) return 0;
        if (!M5.Mic.record(static_cast<int16_t *>(out), frameCount, kRate, /*stereo=*/false))
            return 0;
        while (M5.Mic.isRecording()) delay(1);
        return frameCount;
    }
private:
    PCMFormat format_{};
};

// ---- Globals --------------------------------------------------------------
static PCMFlow      audio;     // receive-side pipeline
static OpusEncoder  enc;
static OpusDecoder  dec;
static M5MicSource  mic;
static Player       player;

static void showStatus(const char *text, uint16_t color = TFT_DARKGREY)
{
    M5.Display.fillRect(0, 0, M5.Display.width(), 24, color);
    M5.Display.setCursor(4, 4);
    M5.Display.print(text);
}

void setup()
{
    auto cfg = M5.config();
    M5.begin(cfg);
    Serial.begin(115200);

    M5.Display.setTextSize(2);
    M5.Display.fillScreen(TFT_BLACK);
    showStatus("init…");

    if (!espNowBegin()) {
        Serial.println("ESP-NOW init failed");
        showStatus("ESP-NOW NG", TFT_RED);
        return;
    }

    M5.Mic.begin();
    // en: Initialize this component after its prerequisites; inspect the returned status where provided.
    // ja: 前提となる機器を準備してから初期化し、結果を返すAPIでは成否を確認します。
    if (!mic.begin()) {
        Serial.println("M5.Mic.begin failed");
        showStatus("MIC NG", TFT_RED);
        return;
    }

    M5.Speaker.begin();
    M5.Speaker.setVolume(180);  // 0..255

    if (!enc.begin({kRate, kChannels, 16}, OpusApplication::Voip, kBitrate)) {
        Serial.println("OpusEncoder.begin failed");
        showStatus("ENC NG", TFT_RED);
        return;
    }
    // FEC + assume some loss in radio-style transport.
    enc.setInbandFec(true);
    enc.setPacketLossPerc(10);

    if (!dec.begin({kRate, kChannels, 16})) {
        Serial.println("OpusDecoder.begin failed");
        showStatus("DEC NG", TFT_RED);
        return;
    }

    audio.setInputSource(dec);
    audio.setOutputFormat({kRate, kChannels, 16});
    audio.setBufferFrames(1024);   // ~64 ms of jitter slack

    if (!player.begin({kRate, kChannels, 16}, Player::stableProfile())) {
        Serial.println("M5SpeakerBufferedPlayer.begin failed");
        showStatus("PLAYER NG", TFT_RED);
        return;
    }

    showStatus("listen — hold A to talk");
    Serial.printf("Ready. MAC=%s\n", WiFi.macAddress().c_str());
}

static void doTalk()
{
    // One 20 ms frame: capture -> encode -> broadcast.
    static int16_t pcm[kFrame];
    // en: Use the actual returned frame count; bytes also depend on channel count and sample width.
    // ja: 実際に返ったフレーム数を使います。Byte数にはチャンネル数とサンプル幅も関係します。
    if (mic.readFrames(pcm, kFrame) != kFrame) return;

    static uint8_t pkt[400];
    // en: Encode one supported frame and use the actual encoded length.
    // ja: 対応する1Frameを符号化し、実際の符号化長を使います。
    const size_t n = enc.encodeFrame(pcm, kFrame, pkt, sizeof(pkt));
    if (n == 0) return;

    esp_now_send(kBroadcastMac, pkt, n);
}

static void doListen()
{
    // Drain any queued ESP-NOW packets into the decoder.
    while (g_rxTail != g_rxHead) {
        RxPacket *slot = const_cast<RxPacket *>(&g_rxRing[g_rxTail]);
        // en: Decode the actual packet length and inspect errors or available output.
        // ja: 実際のPacket長で復号し、Errorや出力量を確認します。
        dec.decodePacket(slot->buf, slot->len, nullptr, 0);
        g_rxTail = (g_rxTail + 1) % kRxRingSlots;

        // Drain whatever PCM this packet produced before refilling the
        // decoder's single-frame buffer with the next packet.
        static int16_t out[kFrame];
        while (true) {
            // en: Advance decoding/conversion, then consume only the available returned PCM frames.
            // ja: 復号／変換を進め、得られたPCM フレームだけを取り出します。
            audio.pump();
            const size_t got = audio.readFrames(out, kFrame);
            if (got == 0) break;
            // en: Check the number accepted and retain any unconsumed tail; a frame is not a byte.
            // ja: 受理量を確認し、残りを保持します。フレーム数とByte数は異なります。
            player.writeFrames(out, got);
        }
    }
}

void loop()
{
    M5.update();

    const bool talking = M5.BtnA.isPressed();
    static bool was_talking = false;
    if (talking != was_talking) {
        showStatus(talking ? "TALK"   : "listen — hold A to talk",
                   talking ? TFT_RED  : TFT_DARKGREY);
        was_talking = talking;
    }

    if (talking) {
        doTalk();
    } else {
        doListen();
        delay(1);  // yield while idle
    }
}
