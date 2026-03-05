from __future__ import annotations

from dataclasses import dataclass
from typing import Any

from instance_manager import PrismRuntimeManager


@dataclass(frozen=True)
class TargetRef:
    target_kind: str
    target_id: str


class SimTargetAdapter:
    def __init__(self, manager: PrismRuntimeManager):
        self.manager = manager

    def get_status(self, target_id: str) -> dict:
        self.manager.ensure_instance(target_id)
        return self.manager.get_status(target_id)

    def get_config(self, target_id: str) -> dict:
        self.manager.ensure_instance(target_id)
        return self.manager.get_config(target_id)


class RealTargetAdapter:
    def get_status(self, target_id: str) -> dict:
        raise NotImplementedError(f"Real target adapter not configured for target_id={target_id}")

    def get_config(self, target_id: str) -> dict:
        raise NotImplementedError(f"Real target adapter not configured for target_id={target_id}")


class TargetRouter:
    def __init__(self, sim_adapter: SimTargetAdapter, real_adapter: RealTargetAdapter):
        self.sim_adapter = sim_adapter
        self.real_adapter = real_adapter

    @staticmethod
    def _value(query: dict[str, list[str]], body: dict[str, Any], key: str) -> str | None:
        if key in query and len(query[key]) > 0:
            return str(query[key][0])
        value = body.get(key)
        if value is None:
            return None
        return str(value)

    def resolve_target(self, query: dict[str, list[str]], body: dict[str, Any], default_target_id: str) -> TargetRef:
        target_kind = (self._value(query, body, "target_kind") or "sim").strip().lower()
        target_id = (
            self._value(query, body, "target_id")
            or self._value(query, body, "instance_id")
            or default_target_id
        )
        return TargetRef(target_kind=target_kind, target_id=str(target_id))

    def get_adapter(self, target_kind: str):
        if target_kind == "sim":
            return self.sim_adapter
        if target_kind == "real":
            return self.real_adapter
        raise ValueError(f"Unsupported target_kind: {target_kind}")
