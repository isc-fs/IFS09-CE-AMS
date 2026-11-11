// SPDX-License-Identifier: proprietary
//
// Cell open-wire (LTC6811 ADOW) tests: the command encoder and the pure
// detection algorithm. Hardware timing / the two RDCV passes are HIL-gated
// (config::CellOpenWireCheck); the logic here is what must be provably correct.

#include "ltc6811.hpp"
#include "open_wire.hpp"
#include "ams_config.hpp"
#include "bms_service.hpp"
#include "safety_predicates.hpp"

#include "unity.h"

#include <cstdint>

namespace {
constexpr std::uint16_t DELTA = ams::config::CellOpenWireDeltaMv;  // 400 mV
}

// --- ADOW command encoding (datasheet-derived; see ltc6811.hpp adow_cmd) -----
extern "C" void test_adow_cmd_encoding(void) {
    using ams::ltc6811::adow_cmd;
    using ams::ltc6811::AdcMode;
    using ams::ltc6811::CellSel;
    // MD=Norm7kHz(10), PUP=1, DCP=0, CH=All -> 0x0228|0x100|0x040 = 0x0368
    TEST_ASSERT_EQUAL_HEX16(0x0368,
        adow_cmd(AdcMode::Norm7kHz, /*pull_up=*/true,  /*dcp=*/false, CellSel::All));
    // Same but PUP=0 -> 0x0328. NOT 0x0348: PUP lives on bit6 and bit5 is a
    // fixed 1, so clearing PUP must clear b6 and LEAVE b5 set. The old 0x0348
    // (b6 set, b5 clear) is not a valid ADOW -- the chain ignored it and RDCV
    // returned the stale pull-up conversion, which is why PU == PD on hardware.
    TEST_ASSERT_EQUAL_HEX16(0x0328,
        adow_cmd(AdcMode::Norm7kHz, /*pull_up=*/false, /*dcp=*/false, CellSel::All));
    // DCP=1 sets bit4 (+0x10)
    TEST_ASSERT_EQUAL_HEX16(0x0378,
        adow_cmd(AdcMode::Norm7kHz, /*pull_up=*/true,  /*dcp=*/true,  CellSel::All));
}

// --- detect_open_conductors: no open on a healthy IC -------------------------
extern "C" void test_open_wire_none_when_healthy(void) {
    // Pull-up and pull-down readings track each other on a connected cell.
    std::uint16_t pu[10], pd[10];
    for (int i = 0; i < 10; ++i) { pu[i] = 3700; pd[i] = 3700; }
    TEST_ASSERT_EQUAL_UINT16(0u, ams::open_wire::detect_open_conductors(pu, pd, 10, DELTA));
    TEST_ASSERT_FALSE(ams::open_wire::has_open(pu, pd, 10, DELTA));
}

// --- interior conductor open: CELL_PU(n+1) - CELL_PD(n+1) < -delta -----------
extern "C" void test_open_wire_interior_conductor(void) {
    std::uint16_t pu[10], pd[10];
    for (int i = 0; i < 10; ++i) { pu[i] = 3700; pd[i] = 3700; }
    // Conductor 4 open: cell index 4's pull-up reading collapses below pull-down.
    pu[4] = 3000; pd[4] = 3700;   // diff = -700 < -400
    const std::uint16_t mask = ams::open_wire::detect_open_conductors(pu, pd, 10, DELTA);
    TEST_ASSERT_BITS_HIGH(1u << 4, mask);
    TEST_ASSERT_TRUE(ams::open_wire::has_open(pu, pd, 10, DELTA));
    // A delta smaller than the threshold does NOT flag.
    pu[4] = 3400; pd[4] = 3700;   // diff = -300 > -400
    TEST_ASSERT_EQUAL_UINT16(0u, ams::open_wire::detect_open_conductors(pu, pd, 10, DELTA));
}

// --- endpoint conductors: C(0) via CELL_PU(1)==0, C(N) via CELL_PD(N)==0 -----
extern "C" void test_open_wire_endpoints(void) {
    std::uint16_t pu[10], pd[10];
    for (int i = 0; i < 10; ++i) { pu[i] = 3700; pd[i] = 3700; }
    pu[0] = 0;   // bottom conductor C(0) open
    TEST_ASSERT_BITS_HIGH(1u << 0, ams::open_wire::detect_open_conductors(pu, pd, 10, DELTA));

    for (int i = 0; i < 10; ++i) { pu[i] = 3700; pd[i] = 3700; }
    pd[9] = 0;   // top conductor C(N) open (N = n_cells = 10)
    TEST_ASSERT_BITS_HIGH(1u << 10, ams::open_wire::detect_open_conductors(pu, pd, 10, DELTA));
}

// --- REAL BENCH DATA (feat/adow-raw-diagnostic, module 2 upper LTC) ----------
// Captured over pit-diag 0x6D0/0x6E8 with the corrected adow_cmd encoding, on a
// live ~356 V pack. Upper LTC of a module carries 9 cells (module cells 0..8).
// These two vectors are the ground truth the encoding fix was validated against;
// if a future change breaks ADOW again, these fail before the car does.
extern "C" void test_open_wire_bench_healthy_no_false_positive(void) {
    // Pack intact. Note c9 (index 8, the LAST cell of the LTC) sits at -127 mV:
    // the pull current has no cell above it, so the end cells always shift
    // hardest. That is a structural artifact, NOT an open -- it must stay well
    // inside DELTA. Margin here is ~3.1x; dropping CellOpenWireDeltaMv below
    // ~130 mV would false-fault the last cell of every LTC in the pack.
    const std::uint16_t pu[9] = {3836, 3856, 3854, 3856, 3855, 3856, 3856, 3816, 3728};
    const std::uint16_t pd[9] = {3788, 3857, 3855, 3856, 3856, 3857, 3857, 3816, 3855};
    TEST_ASSERT_EQUAL_UINT16(0u, ams::open_wire::detect_open_conductors(pu, pd, 9, DELTA));
}

extern "C" void test_open_wire_bench_open_conductor_detected(void) {
    // Same module with the c2/c3 sense wire physically disconnected. The shared
    // node floats: c2 rails high under pull-up and low under pull-down while c3
    // does the exact opposite, and the PAIR SUM stays ~2 cells (this is why the
    // tap-artifact guard masks it from the voltage-range path -- ADOW is the
    // only predicate that can see it).
    const std::uint16_t pu[9] = {3836, 5474, 1264, 3855, 3855, 3856, 3856, 3816, 3728};
    const std::uint16_t pd[9] = {3788, 1240, 5473, 3856, 3856, 3857, 3857, 3816, 3856};
    const std::uint16_t mask = ams::open_wire::detect_open_conductors(pu, pd, 9, DELTA);
    // Conductor 2 (between cell 2 and cell 3) is the open one -- and ONLY that
    // one: the +4234 mV swing on c2 must not also flag conductor 1.
    TEST_ASSERT_EQUAL_UINT16(1u << 2, mask);
    TEST_ASSERT_TRUE(ams::open_wire::has_open(pu, pd, 9, DELTA));
}

// --- conductor -> affected-cell mapping (drives the 0x680 no-data sentinel) --
// Mirrors the loop in BmsService::update_open_wire. Conductor k is the node
// between IC-local cells k-1 and k, so an interior open corrupts BOTH; the
// endpoints border a single cell each.
static std::uint32_t affected_cells(std::uint16_t conductors,
                                    std::uint8_t n_cells, std::uint8_t base) {
    std::uint32_t cells = 0u;
    for (std::uint8_t k = 0; k <= n_cells; ++k) {
        if ((conductors & (1u << k)) == 0u) continue;
        if (k > 0u)       cells |= (1u << (base + k - 1u));
        if (k < n_cells)  cells |= (1u << (base + k));
    }
    return cells;
}

extern "C" void test_open_wire_conductor_to_cell_mapping(void) {
    // The real bench fault: conductor 2 of the UPPER LTC (base 0, 9 cells).
    // Must flag module cells 1 and 2 -- i.e. the c2/c3 pair we measured.
    TEST_ASSERT_EQUAL_UINT32((1u << 1) | (1u << 2), affected_cells(1u << 2, 9, 0));

    // Bottom endpoint C(0) borders only cell 0.
    TEST_ASSERT_EQUAL_UINT32(1u << 0, affected_cells(1u << 0, 9, 0));
    // Top endpoint C(9) on a 9-cell IC borders only cell 8.
    TEST_ASSERT_EQUAL_UINT32(1u << 8, affected_cells(1u << 9, 9, 0));

    // Lower LTC (10 cells) is offset to module cells 9..18: conductor 1 there
    // flags module cells 9 and 10, NOT 0 and 1.
    TEST_ASSERT_EQUAL_UINT32((1u << 9) | (1u << 10), affected_cells(1u << 1, 10, 9));
    // Its top endpoint C(10) is module cell 18 -- the last real cell, never 19.
    TEST_ASSERT_EQUAL_UINT32(1u << 18, affected_cells(1u << 10, 10, 9));
}

// --- guards: null / zero / oversized never report a (false) open -------------
extern "C" void test_open_wire_input_guards(void) {
    std::uint16_t pu[10] = {0}, pd[10] = {0};
    TEST_ASSERT_EQUAL_UINT16(0u, ams::open_wire::detect_open_conductors(nullptr, pd, 10, DELTA));
    TEST_ASSERT_EQUAL_UINT16(0u, ams::open_wire::detect_open_conductors(pu, nullptr, 10, DELTA));
    TEST_ASSERT_EQUAL_UINT16(0u, ams::open_wire::detect_open_conductors(pu, pd, 0, DELTA));
    TEST_ASSERT_EQUAL_UINT16(0u, ams::open_wire::detect_open_conductors(pu, pd, 99, DELTA));
}

// --- Fast Re-check / Debounce Unit Tests -----------------------------------

// a) A single spurious open-wire detection sample must NOT trip safety (SDC remains intact).
extern "C" void test_open_wire_spurious_single_sample_does_not_trip_safety(void) {
    // 1. Validate DebounceTracker logic
    ams::open_wire::DebounceTracker tracker;
    tracker.update(0x01, ams::config::OpenWireConfirmSamples);
    TEST_ASSERT_EQUAL(static_cast<std::uint8_t>(ams::open_wire::Status::Suspect),
                      static_cast<std::uint8_t>(tracker.status));
    TEST_ASSERT_EQUAL_UINT8(1u, tracker.consecutive_fails);
    TEST_ASSERT_EQUAL_UINT8(0x01, tracker.suspect_mask);
    TEST_ASSERT_EQUAL_UINT8(0x00, tracker.confirmed_mask);

    // 2. Validate Safety Predicate suppression while Suspect
    ams::BmsState bms{};
    bms.module_online_mask          = ams::config::AllModulesMask;
    for (auto& t : bms.last_rx_tick) t = 10000;
    bms.cell_open_mask              = 0x00;  // Suppressed while suspect
    bms.cell_open_suspect_mask      = 0x01;
    bms.open_wire_status            = ams::open_wire::Status::Suspect;
    bms.open_wire_consecutive_fails = 1u;

    ams::CurrentState cur{};
    ams::VehicleState veh{};
    ams::safety::Inputs in{bms, cur, veh, /*force_error_set=*/false,
                           /*vcu_required=*/false, /*charger_required=*/false,
                           /*now_tick=*/10000};

    const auto fault = ams::safety::evaluate_fault_detail(in);
    TEST_ASSERT_NOT_EQUAL(static_cast<std::uint8_t>(ams::safety::FaultReason::CellOpenWire),
                          static_cast<std::uint8_t>(fault.reason));
}

// b) N consecutive samples with an open wire confirm the fault and trip safety (opens SDC).
extern "C" void test_open_wire_n_consecutive_samples_trips_safety(void) {
    ams::open_wire::DebounceTracker tracker;
    for (std::uint8_t i = 1; i <= ams::config::OpenWireConfirmSamples; ++i) {
        tracker.update(0x04, ams::config::OpenWireConfirmSamples);
        if (i < ams::config::OpenWireConfirmSamples) {
            TEST_ASSERT_EQUAL(static_cast<std::uint8_t>(ams::open_wire::Status::Suspect),
                              static_cast<std::uint8_t>(tracker.status));
            TEST_ASSERT_EQUAL_UINT8(0x00, tracker.confirmed_mask);
        } else {
            TEST_ASSERT_EQUAL(static_cast<std::uint8_t>(ams::open_wire::Status::Fault),
                              static_cast<std::uint8_t>(tracker.status));
            TEST_ASSERT_EQUAL_UINT8(0x04, tracker.confirmed_mask);
        }
    }

    // Validate Safety Predicate trips when confirmed
    ams::BmsState bms{};
    bms.module_online_mask          = ams::config::AllModulesMask;
    for (auto& t : bms.last_rx_tick) t = 10000;
    bms.cell_open_mask              = 0x04;
    bms.cell_open_suspect_mask      = 0x04;
    bms.open_wire_status            = ams::open_wire::Status::Fault;
    bms.open_wire_consecutive_fails = ams::config::OpenWireConfirmSamples;

    ams::CurrentState cur{};
    ams::VehicleState veh{};
    ams::safety::Inputs in{bms, cur, veh, /*force_error_set=*/false,
                           /*vcu_required=*/false, /*charger_required=*/false,
                           /*now_tick=*/10000};

    if (ams::config::CellOpenWireCheck) {
        const auto fault = ams::safety::evaluate_fault_detail(in);
        TEST_ASSERT_EQUAL(static_cast<std::uint8_t>(ams::safety::FaultReason::CellOpenWire),
                          static_cast<std::uint8_t>(fault.reason));
        TEST_ASSERT_EQUAL_UINT8(0x04, fault.detail);
    }
}

// c) A sequence Fault -> OK -> Fault resets the counter and does not accumulate streak.
extern "C" void test_open_wire_fault_ok_fault_sequence_resets_streak(void) {
    ams::open_wire::DebounceTracker tracker;

    // Step 1: 1st fault sample -> Suspect (count = 1)
    tracker.update(0x02, ams::config::OpenWireConfirmSamples);
    TEST_ASSERT_EQUAL(static_cast<std::uint8_t>(ams::open_wire::Status::Suspect),
                      static_cast<std::uint8_t>(tracker.status));
    TEST_ASSERT_EQUAL_UINT8(1u, tracker.consecutive_fails);

    // Step 2: Clean sample (OK) -> Clear (count resets to 0)
    tracker.update(0x00, ams::config::OpenWireConfirmSamples);
    TEST_ASSERT_EQUAL(static_cast<std::uint8_t>(ams::open_wire::Status::Clear),
                      static_cast<std::uint8_t>(tracker.status));
    TEST_ASSERT_EQUAL_UINT8(0u, tracker.consecutive_fails);
    TEST_ASSERT_EQUAL_UINT8(0x00, tracker.suspect_mask);
    TEST_ASSERT_EQUAL_UINT8(0x00, tracker.confirmed_mask);

    // Step 3: Fault sample again -> Suspect (count is 1, NOT 2)
    tracker.update(0x02, ams::config::OpenWireConfirmSamples);
    TEST_ASSERT_EQUAL(static_cast<std::uint8_t>(ams::open_wire::Status::Suspect),
                      static_cast<std::uint8_t>(tracker.status));
    TEST_ASSERT_EQUAL_UINT8(1u, tracker.consecutive_fails);
    TEST_ASSERT_EQUAL_UINT8(0x00, tracker.confirmed_mask);
}
