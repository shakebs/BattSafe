import sys
import unittest
import copy
from pathlib import Path


ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / "7_Demo" / "dashboard" / "src"))
sys.path.insert(0, str(ROOT / "7_Demo"))

from virtual_board import VirtualVsdsquadron  # noqa: E402
from digital_twin.config import FaultType, SIM_DT  # noqa: E402
from digital_twin.physics_engine import BatteryPack  # noqa: E402
from digital_twin.fault_injection import FaultInjectionEngine  # noqa: E402


def step_sim(pack: BatteryPack, faults: FaultInjectionEngine, steps: int) -> None:
    for _ in range(steps):
        faults.apply_faults(SIM_DT)
        pack.step(SIM_DT)


class VirtualBoardTests(unittest.TestCase):
    def setUp(self) -> None:
        self.pack = BatteryPack()
        self.faults = FaultInjectionEngine(self.pack)
        self.processor = VirtualVsdsquadron()

    def test_normal_operation_stays_normal(self) -> None:
        last = None
        for _ in range(120):
            step_sim(self.pack, self.faults, 1)
            last = self.processor.process_snapshot(self.pack.get_snapshot())

        self.assertIsNotNone(last)
        self.assertEqual(last["system_state"], "NORMAL")
        self.assertEqual(last["intelligent_detection"]["anomaly_count"], 0)
        self.assertEqual(last["raw_data"]["total_channels"], 139)
        self.assertLess(last["thermal_runaway_prediction"]["risk_factor"], 0.25)

    def test_sensor_drift_is_single_category_warning(self) -> None:
        self.faults.inject_fault(
            fault_type=FaultType.SENSOR_DRIFT,
            module=2,
            group=1,
            severity=1.0,
            duration=0,
        )

        states = set()
        seen_categories = set()
        emergency_direct_seen = False

        for _ in range(180):
            step_sim(self.pack, self.faults, 1)
            out = self.processor.process_snapshot(self.pack.get_snapshot())
            states.add(out["system_state"])
            seen_categories.update(out["intelligent_detection"]["categories"])
            emergency_direct_seen = emergency_direct_seen or out["intelligent_detection"]["emergency_direct"]

        self.assertIn("thermal", seen_categories)
        self.assertIn("WARNING", states)
        self.assertNotIn("EMERGENCY", states)
        self.assertFalse(emergency_direct_seen)

    def test_thermal_runaway_fault_escalates_and_raises_risk(self) -> None:
        self.faults.inject_fault(
            fault_type=FaultType.THERMAL_RUNAWAY,
            module=4,
            group=6,
            severity=1.0,
            duration=0,
        )

        states = []
        categories_seen = set()
        peak_risk = 0.0
        last = None

        for _ in range(600):
            step_sim(self.pack, self.faults, 1)
            last = self.processor.process_snapshot(self.pack.get_snapshot())
            states.append(last["system_state"])
            categories_seen.update(last["intelligent_detection"]["categories"])
            peak_risk = max(peak_risk, last["thermal_runaway_prediction"]["risk_factor"])

        self.assertIsNotNone(last)
        self.assertTrue(any(s in {"CRITICAL", "EMERGENCY"} for s in states))
        self.assertIn("thermal", categories_seen)
        self.assertGreaterEqual(max(len(categories_seen), last["intelligent_detection"]["anomaly_count"]), 2)
        self.assertGreater(peak_risk, 0.60)
        self.assertIn("M", last["thermal_runaway_prediction"]["hottest"])

    def test_raw_payload_contains_deviation_and_outliers(self) -> None:
        step_sim(self.pack, self.faults, 20)
        out = self.processor.process_snapshot(self.pack.get_snapshot())
        raw = out["raw_data"]

        self.assertIn("deviation", raw)
        self.assertIn("outliers", raw)
        self.assertIn("module_scores", raw["outliers"])
        self.assertGreaterEqual(len(raw["outliers"]["module_scores"]), 1)

    def test_emergency_recovers_to_normal_after_nominal_inputs(self) -> None:
        step_sim(self.pack, self.faults, 5)
        baseline = self.pack.get_snapshot()

        emergency = copy.deepcopy(baseline)
        emergency["sim_time"] = float(baseline.get("sim_time", 0.0)) + SIM_DT
        emergency["pack_current"] = 620.0  # emergency_direct threshold
        out = self.processor.process_snapshot(emergency)

        self.assertEqual(out["system_state"], "EMERGENCY")
        self.assertTrue(self.processor.emergency_latched)

        final = None
        for i in range(self.processor.emergency_recovery_limit + 2):
            nominal = copy.deepcopy(baseline)
            nominal["sim_time"] = emergency["sim_time"] + ((i + 1) * 0.5)
            final = self.processor.process_snapshot(nominal)

        self.assertIsNotNone(final)
        self.assertEqual(final["system_state"], "NORMAL")
        self.assertFalse(self.processor.emergency_latched)


if __name__ == "__main__":
    unittest.main()
