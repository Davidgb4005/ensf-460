import csv
import time
from enum import Enum
from pathlib import Path


class TwinEventLogger:
    def __init__(self, path):
        self.path = path
        self.start_time = time.perf_counter()
        self.previous = None
        self.columns = (
            "time_s",
            "source",
            "event",
            "diff",
            "digital_state",
            "digital_pb1",
            "digital_pb2",
            "digital_pb3",
            "digital_led1_state",
            "digital_led1_output",
            "digital_led2_state",
            "digital_led2_output",
            "digital_preset",
            "digital_elapsed",
            "physical_state",
            "physical_pb1",
            "physical_pb2",
            "physical_pb3",
            "physical_led1",
            "physical_led2",
        )
        self._write_header()

    def poll(self, digital, physical):
        if physical is not None and digital is not None and hasattr(physical, "sync_expected"):
            physical.sync_expected(digital)

        current = {
            "digital": self._digital_state(digital),
            "physical": self._physical_state(physical),
        }
        if self.previous is None:
            self._write("Logger", "START", current)
            self.previous = current
            return

        for event in self._changes(self.previous, current):
            self._write(event[0], event[1], current)

        self.previous = current

    def _changes(self, previous, current):
        labels = {
            ("digital", "state"): "DigitalTwin state",
            ("digital", "pb1"): "DigitalTwin PB1",
            ("digital", "pb2"): "DigitalTwin PB2",
            ("digital", "pb3"): "DigitalTwin PB3",
            ("physical", "state"): "PhysicalTwin state",
            ("physical", "pb1"): "PhysicalTwin PB1",
            ("physical", "pb2"): "PhysicalTwin PB2",
            ("physical", "pb3"): "PhysicalTwin PB3",
        }

        events = []
        for (twin_name, key), label in labels.items():
            old = previous[twin_name].get(key)
            new = current[twin_name].get(key)
            if old != new:
                events.append((label, f"{old}->{new}"))
        return events

    def _write(self, source, event, state):
        timestamp = time.perf_counter() - self.start_time
        digital = state["digital"]
        physical = state["physical"]
        row = {
            "time_s": f"{timestamp:.5f}",
            "source": source,
            "event": event,
            "diff": self._diff(digital, physical),
            "digital_state": digital.get("state"),
            "digital_pb1": digital.get("pb1"),
            "digital_pb2": digital.get("pb2"),
            "digital_pb3": digital.get("pb3"),
            "digital_led1_state": digital.get("led1_state"),
            "digital_led1_output": digital.get("led1_output"),
            "digital_led2_state": digital.get("led2_state"),
            "digital_led2_output": digital.get("led2_output"),
            "digital_preset": digital.get("preset"),
            "digital_elapsed": digital.get("elapsed"),
            "physical_state": physical.get("state"),
            "physical_pb1": physical.get("pb1"),
            "physical_pb2": physical.get("pb2"),
            "physical_pb3": physical.get("pb3"),
            "physical_led1": physical.get("led1"),
            "physical_led2": physical.get("led2"),
        }
        with open(self.path, "a", newline="", encoding="utf-8") as log_file:
            writer = csv.DictWriter(log_file, fieldnames=self.columns)
            writer.writerow(row)

    def _digital_state(self, twin):
        if twin is None:
            return {}

        return {
            "state": self._name(getattr(twin, "state", None)),
            "pb1": self._name(getattr(twin.pb_1, "state", None)),
            "pb2": self._name(getattr(twin.pb_2, "state", None)),
            "pb3": self._name(getattr(twin.pb_3, "state", None)),
            "led1_state": self._name(getattr(twin.led_1, "state", None)),
            "led1_output": self._name(getattr(twin.led_1, "output", None)),
            "led2_state": self._name(getattr(twin.led_2, "state", None)),
            "led2_output": self._name(getattr(twin.led_2, "output", None)),
            "preset": getattr(twin, "preset_timer", None),
            "elapsed": getattr(twin, "elapsed_timer", None),
        }

    def _physical_state(self, twin):
        if twin is None:
            return {}

        if hasattr(twin, "sample_state"):
            twin.sample_state()
        for component in (twin.pb_1, twin.pb_2, twin.pb_3, twin.led_1, twin.led_2):
            if hasattr(component, "sample"):
                component.sample()

        return {
            "state": self._name(getattr(twin, "output_physical_state", None)),
            "pb1": self._name(getattr(twin.pb_1, "output_physical", None)),
            "pb2": self._name(getattr(twin.pb_2, "output_physical", None)),
            "pb3": self._name(getattr(twin.pb_3, "output_physical", None)),
            "led1": self._name(getattr(twin.led_1, "output_physical", None)),
            "led2": self._name(getattr(twin.led_2, "output_physical", None)),
        }

    def _diff(self, digital, physical):
        comparisons = (
            ("state", digital.get("state"), physical.get("state")),
            ("pb1", digital.get("pb1"), physical.get("pb1")),
            ("pb2", digital.get("pb2"), physical.get("pb2")),
            ("pb3", digital.get("pb3"), physical.get("pb3")),
            ("led1", digital.get("led1_output"), physical.get("led1")),
            ("led2", digital.get("led2_output"), physical.get("led2")),
        )
        diffs = [
            f"{name}:D={digital_value}/P={physical_value}"
            for name, digital_value, physical_value in comparisons
            if digital_value != physical_value
        ]
        return "; ".join(diffs) if diffs else "OK"

    def _write_header(self):
        Path(self.path).parent.mkdir(parents=True, exist_ok=True)
        with open(self.path, "a", newline="", encoding="utf-8") as log_file:
            writer = csv.DictWriter(log_file, fieldnames=self.columns)
            writer.writeheader()

    def _name(self, value):
        if value is None:
            return "N/A"
        if isinstance(value, Enum):
            return value.name.replace("TIMER_", "").replace("GPIO_", "")
        return str(value)
