from enum import Enum


class Tui:
    def __init__(self, panel_width=44):
        self.panel_width = panel_width
        self._last_height = 0

    def print(self, digital, physical):
        if physical is not None and digital is not None and hasattr(physical, "sync_expected"):
            physical.sync_expected(digital)

        columns = [
            self.digital_twin_panel(digital),
            self.physical_twin_panel(physical),
            self.diff_panel(digital, physical),
        ]
        height = max(len(column) for column in columns)
        padded_columns = [
            column + [" " * self.panel_width] * (height - len(column))
            for column in columns
        ]

        rows = ["  ".join(row) for row in zip(*padded_columns)]
        clear_rows = max(self._last_height - len(rows), 0)

        print("\033[?25l\033[H", end="")
        for row in rows:
            print(f"{row}\033[K")
        for _ in range(clear_rows):
            print("\033[K")
        print("\033[?25h", end="", flush=True)
        self._last_height = len(rows)

    def digital_twin_panel(self, twin):
        return self._panel("DIGITAL TWIN", self._digital_snapshot_lines(self._twin_snapshot(twin)))

    def physical_twin_panel(self, twin):
        return self._panel("PHYSICAL TWIN", self._physical_snapshot_lines(self._twin_snapshot(twin)))

    def diff_panel(self, digital, physical):
        if physical is not None and digital is not None and hasattr(physical, "sync_expected"):
            physical.sync_expected(digital)
        digital_snapshot = self._twin_snapshot(digital)
        physical_snapshot = self._twin_snapshot(physical)

        if digital_snapshot is None or physical_snapshot is None:
            lines = [
                "No physical twin loaded",
                "Digital-only simulation",
                "Diff unavailable",
            ]
        else:
            labels = (
                ("PB1", "pb1_digital", "pb1_physical"),
                ("PB2", "pb2_digital", "pb2_physical"),
                ("PB3", "pb3_digital", "pb3_physical"),
                ("LED1", "led1_digital", "led1_physical"),
                ("LED2", "led2_digital", "led2_physical"),
                ("State", "stopwatch_state", "physical_state"),
            )
            lines = []
            for label, digital_key, physical_key in labels:
                if digital_snapshot[digital_key] == physical_snapshot[physical_key]:
                    lines.append(f"{label}: OK")
                else:
                    lines.append(
                        f"{label}: D={digital_snapshot[digital_key]} P={physical_snapshot[physical_key]}"
                    )

        return self._panel("DIFFERENCE", lines)

    def _twin_snapshot(self, twin):
        if twin is None:
            return None
        if hasattr(twin, "sample_state"):
            twin.sample_state()
        pb1 = getattr(twin, "pb_1", None)
        pb2 = getattr(twin, "pb_2", None)
        pb3 = getattr(twin, "pb_3", None)
        led1 = getattr(twin, "led_1", None)
        led2 = getattr(twin, "led_2", None)

        return {
            "stopwatch_state": self._name(getattr(twin, "state", None)),
            "physical_state": self._name(
                getattr(twin, "output_physical_state", getattr(twin, "state", None))
            ),
            "preset_time": getattr(twin, "preset_timer", None),
            "elapsed_time": getattr(twin, "elapsed_timer", None),
            "pb1_digital": self._name(self._component_digital(pb1, "state")),
            "pb1_physical": self._name(self._component_physical(pb1, "state")),
            "pb2_digital": self._name(self._component_digital(pb2, "state")),
            "pb2_physical": self._name(self._component_physical(pb2, "state")),
            "pb3_digital": self._name(self._component_digital(pb3, "state")),
            "pb3_physical": self._name(self._component_physical(pb3, "state")),
            "led1_state": self._name(getattr(led1, "state", None)),
            "led1_digital": self._name(self._component_digital(led1, "output")),
            "led1_physical": self._name(self._component_physical(led1, "output")),
            "led2_state": self._name(getattr(led2, "state", None)),
            "led2_digital": self._name(self._component_digital(led2, "output")),
            "led2_physical": self._name(self._component_physical(led2, "output")),
        }

    def _digital_snapshot_lines(self, snapshot):
        if snapshot is None:
            return [
                "PB:  unavailable",
                "LED: unavailable",
                "State: unavailable",
                "Preset: unavailable",
                "Elapsed: unavailable",
            ]

        return [
            f"PB:   1={snapshot['pb1_digital']} 2={snapshot['pb2_digital']} 3={snapshot['pb3_digital']}",
            f"LED1: state={snapshot['led1_state']}",
            f"      output={snapshot['led1_digital']}",
            f"LED2: state={snapshot['led2_state']}",
            f"      output={snapshot['led2_digital']}",
            f"State: {snapshot['stopwatch_state']}",
            f"Preset: {snapshot['preset_time']}s ({self._time_label(snapshot['preset_time'])})",
            f"Elapsed: {snapshot['elapsed_time']}s ({self._time_label(snapshot['elapsed_time'])})",
        ]

    def _physical_snapshot_lines(self, snapshot):
        if snapshot is None:
            return [
                "PB:  unavailable",
                "LED: unavailable",
            ]

        return [
            f"PB:   1={snapshot['pb1_physical']} 2={snapshot['pb2_physical']} 3={snapshot['pb3_physical']}",
            f"LED1: output={snapshot['led1_physical']}",
            f"LED2: output={snapshot['led2_physical']}",
            f"State: {snapshot['physical_state']}",
        ]

    def _panel(self, title, lines):
        title = f" {title} "
        border = "+" + title.center(self.panel_width - 2, "-") + "+"
        body = [f"| {line[:self.panel_width - 4]:<{self.panel_width - 4}} |" for line in lines]
        return [border, *body, "+" + "-" * (self.panel_width - 2) + "+"]

    def _name(self, value):
        if value is None:
            return "N/A"
        if isinstance(value, Enum):
            return value.name.replace("TIMER_", "").replace("GPIO_", "")
        return str(value)

    def _time_label(self, seconds):
        if seconds is None:
            return "N/A"
        minutes, seconds = divmod(int(seconds), 60)
        return f"{minutes:02d}:{seconds:02d}"

    def _sample(self, component):
        if hasattr(component, "sample"):
            component.sample()

    def _component_digital(self, component, fallback_attr):
        if component is None:
            return None
        return getattr(component, "output_digital", getattr(component, fallback_attr, None))

    def _component_physical(self, component, fallback_attr):
        if component is None:
            return None
        self._sample(component)
        return getattr(component, "output_physical", getattr(component, fallback_attr, None))
