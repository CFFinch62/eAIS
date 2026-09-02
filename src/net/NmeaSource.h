// DERIVED FROM eNMEA/src/net/NmeaSource.h - deliberately divergent.
//
// Same transport, state machine and hard-won fixes (the lwIP socket leak on TCP
// retry, the latched-UDP-state bug), but eNMEA's copy parses straight into its
// dashboard model. This one hands complete sentences to a callback instead, so
// the AIS decoder can consume them. Fixes to the transport belong in both.

#pragma once

#include <WiFiClient.h>
#include <WiFiUdp.h>

#include "nmea/NmeaLineReader.h"
#include "settings/AppSettings.h"

// What the source is actually doing, so the dashboard can say something more
// useful than CONNECTED/WAITING. The distinction that matters in practice is
// "the socket is fine but nothing is arriving" (NoData) vs. "the socket never
// came up" (Failed) - those have completely different causes and the old
// two-state display couldn't tell them apart.
enum class SourceState : uint8_t {
  Idle,        // begin() not called yet
  Listening,   // UDP socket open, no packet has ever arrived
  Connecting,  // TCP dial-out in progress / between retries
  Connected,   // socket up and bytes arriving
  NoData,      // socket up but nothing received for STALL_AFTER_MS
  Failed,      // UDP bind failed, or the TCP connect attempt was refused
};

// Owns the socket (TCP client or UDP listener, per AppSettings::Protocol)
// and feeds whatever bytes arrive into NmeaLineReader -> NmeaParser,
// updating the shared NmeaData/SentenceTable in place.
//
// UDP and TCP behave differently on purpose, matching how real NMEA-over-IP
// multiplexers work: UDP listens locally on `port` for broadcast traffic
// (`host` is unused and ignored), TCP dials out to `host:port` as a client.
// Getting this backwards is the single most common reason no data shows up.
class NmeaSource {
 public:
  bool begin(const NmeaProfile& profile);

  // Closes whatever socket is open and resets state, so a different profile can
  // be applied without rebooting. Leaving the old socket open would hold an
  // lwIP descriptor and, for TCP, keep the previous device connected.
  void end();

  // Called with each complete sentence, in a mutable buffer that is reused
  // between calls - copy anything that must outlive the call.
  using SentenceCallback = void (*)(char* line, void* context);

  // Call every loop() iteration. Reads whatever is available without
  // blocking, retries a dropped/never-established connection periodically.
  void poll(SentenceCallback onSentence, void* context);

  SourceState state() const { return state_; }
  const char* stateText() const;
  bool isConnected() const { return state_ == SourceState::Connected; }

  // millis() of the last byte received from the source, 0 if never.
  unsigned long lastRxMs() const { return lastRxMs_; }
  uint32_t bytesReceived() const { return bytesReceived_; }

 private:
  void pollTcp(SentenceCallback cb, void* ctx);
  void pollUdp(SentenceCallback cb, void* ctx);
  void handleByte(uint8_t c, SentenceCallback cb, void* ctx);
  void noteStall();

  NmeaProfile settings_;
  WiFiClient tcp_;
  WiFiUDP udp_;
  NmeaLineReader lineReader_;
  SourceState state_ = SourceState::Idle;
  unsigned long lastReconnectAttemptMs_ = 0;
  unsigned long lastRxMs_ = 0;
  uint32_t bytesReceived_ = 0;
  int connectAttempts_ = 0;
};
