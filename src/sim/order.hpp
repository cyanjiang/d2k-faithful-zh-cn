#pragma once
// One order record (docs/re/49 section 1.1, docs/re/57 section 1.10):
// the data a player order carries from the UI to ProcessOrder, so it can
// also travel to the other machines of a network game
// (docs/design/multiplayer.md section 3).

#include <cstdint>
#include <vector>

namespace d2k::sim {

// The exe's record types (doc 49 section 3.1, doc 57 section 1.10).
namespace order {
constexpr uint8_t kLeave = 0x09;           // system: a player leaves (doc 57 section 3.8)
constexpr uint8_t kLatency = 0x11;         // system: latency / rate cap / resend time (doc 57 section 1.5)
constexpr uint8_t kMove = 0x13;            // a = x, b = y
constexpr uint8_t kAttackUnit = 0x14;      // a = target unit id
constexpr uint8_t kAttackBuilding = 0x15;  // a = building index
constexpr uint8_t kAttackGround = 0x16;    // a = x, b = y
constexpr uint8_t kDock = 0x17;            // a = building index
constexpr uint8_t kGuard = 0x18;
constexpr uint8_t kScatter = 0x19;
constexpr uint8_t kRetreat = 0x1a;         // a = x, b = y
constexpr uint8_t kAlly = 0x1b;            // a = target side (doc 57 section 3.6)
constexpr uint8_t kStop = 0x1c;            // ours: 0x1c and 0x1d as one record
constexpr uint8_t kTurretUnit = 0x1e;      // a = target unit id
constexpr uint8_t kTurretBuilding = 0x1f;  // a = building index
constexpr uint8_t kPrimary = 0x20;
constexpr uint8_t kRepairUnits = 0x21;     // a = pad building index or -1, b = 0; or a = unit id, b = 1
constexpr uint8_t kRepairBuilding = 0x22;  // a = building index
constexpr uint8_t kSell = 0x23;            // a = building index
constexpr uint8_t kStartBuilding = 0x24;   // a = record
constexpr uint8_t kHoldBuilding = 0x25;    // a = record
constexpr uint8_t kPlace = 0x26;           // a = record, b = x, c = y
constexpr uint8_t kStartUnit = 0x27;       // a = record
constexpr uint8_t kHoldUnit = 0x28;        // a = record
constexpr uint8_t kStarportAdd = 0x29;     // a = record
constexpr uint8_t kStarportRemove = 0x2a;  // a = record
constexpr uint8_t kStarportBuy = 0x2b;
constexpr uint8_t kStarportCancel = 0x2c;
constexpr uint8_t kStartUpgrade = 0x2d;    // a = name index
constexpr uint8_t kHoldUpgrade = 0x2e;     // a = record
constexpr uint8_t kDeploy = 0x2f;          // a = unit id
constexpr uint8_t kSpecial = 0x30;         // a = kind, b = x, c = y
constexpr uint8_t kRetire = 0x31;          // surrender (doc 57 section 3.8)
constexpr uint8_t kAiTakeOver = 0x32;      // LetAITakeOver (doc 57a section 1: no poster in 1.06; the case is kept)
} // namespace order

struct Order {
    uint8_t type = 0;
    uint8_t side = 0;
    uint32_t tick = 0;              // execution tick (set when posted)
    uint32_t seq = 0;               // per-player sequence (network humans only)
    int32_t a = 0, b = 0, c = 0;    // the arguments, by type (above)
    // The side's selected unit ids at post time (ours: unique ids, not the
    // exe's slot indices, doc 49 section 4.1): the first 64 selected units
    // that are not dying, in list order, with the poster's filter
    // (World::capture_selection; the 0x40 loop bound of FUN_00459450 etc.).
    std::vector<int32_t> ids;
    // The side's selected building indices at post time (the exe records
    // carry building ids after the unit ids, doc 49 section 4.1: stop 0x1d,
    // primary, turret orders read them).
    std::vector<int32_t> bids;
};

inline Order make_order(uint8_t type, int side, int a = 0, int b = 0, int c = 0) {
    Order o;
    o.type = type;
    o.side = uint8_t(side);
    o.a = a;
    o.b = b;
    o.c = c;
    return o;
}

} // namespace d2k::sim
