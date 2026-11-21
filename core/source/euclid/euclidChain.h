#pragma once

#include <stddef.h>
#include <stdint.h>

// The chain (the Song key): an arrangement for the live engine. Each step
// plays a set and a scene for some bars; the steps play in order from Play,
// then the chain loops or stops.
//
// File (Sets/chain.chn), little-endian:
//   0 'EUCH', 4 version (1), 5 on, 6 loop, 7 step count,
//   8 kChainSteps steps of 3 bytes: set (0 = stay on the set playing, 1-8),
//     scene (0 = the set's own settings, 1-4 = A-D), bars (1-64),
//   then a CRC-32 of everything before it.
//
// Platform independent, so it's tested on a host (core/tests/euclid/).

namespace euclid
{

constexpr int kChainSteps = 24;
constexpr uint8_t kChainMaxBars = 64;
constexpr uint8_t kChainVersion = 1;
constexpr size_t kChainFileSize = 8 + kChainSteps * 3 + 4;

struct ChainStep
{
    uint8_t set = 0;   // 0 = stay, 1-8
    uint8_t scene = 0; // 0 = the set's own, 1-4 = A-D
    uint8_t bars = 4;  // 1-64
};

struct Chain
{
    bool on = false;
    bool loop = true;
    uint8_t count = 1; // 1 .. kChainSteps
    ChainStep steps[kChainSteps];

    uint32_t totalBars() const;
};

// The step playing in `bar` (0 = the first bar after Play), and the bar that
// step started in. -1 once a chain that doesn't loop has ended.
int chainStepAt(const Chain& c, uint32_t bar, uint32_t* stepStart = nullptr);

size_t serializeChain(const Chain& c, uint8_t* buf, size_t length);
bool deserializeChain(Chain& c, const uint8_t* buf, size_t length);

} // namespace euclid
