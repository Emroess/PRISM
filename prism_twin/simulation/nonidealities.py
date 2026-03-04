from __future__ import annotations

from dataclasses import dataclass
from collections import deque
from typing import Deque


@dataclass
class ActuatorNonidealityConfig:
    enabled: bool = False
    first_order_lag_tau_s: float = 0.0
    slew_rate_nm_per_s: float = 0.0
    command_delay_s: float = 0.0
    torque_limit_nm: float | None = None


@dataclass
class SensorNonidealityConfig:
    enabled: bool = False
    position_quantization_deg: float = 0.0
    velocity_quantization_rad_s: float = 0.0
    delay_s: float = 0.0


class DelayLine:
    def __init__(self, delay_s: float, sample_dt_s: float):
        self.delay_s = max(0.0, float(delay_s))
        self.sample_dt_s = max(1e-9, float(sample_dt_s))
        self._n_delay = int(round(self.delay_s / self.sample_dt_s))
        self._queue: Deque[float] = deque([0.0] * (self._n_delay + 1), maxlen=self._n_delay + 1)

    def push_pop(self, value: float) -> float:
        self._queue.append(float(value))
        return self._queue[0]


def quantize(value: float, quantum: float) -> float:
    if quantum <= 0.0:
        return value
    return round(value / quantum) * quantum


def first_order_lag_step(target: float, state: float, dt_s: float, tau_s: float) -> float:
    if tau_s <= 0.0:
        return target
    alpha = dt_s / (tau_s + dt_s)
    return state + alpha * (target - state)


def slew_rate_limit_step(target: float, prev: float, dt_s: float, slew_nm_per_s: float) -> float:
    if slew_nm_per_s <= 0.0:
        return target
    max_delta = slew_nm_per_s * dt_s
    delta = target - prev
    if delta > max_delta:
        return prev + max_delta
    if delta < -max_delta:
        return prev - max_delta
    return target