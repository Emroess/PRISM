"""
Configuration dataclasses for STEVE valve parameters.
"""

from dataclasses import dataclass, field
from typing import Optional


@dataclass
class ValveConfig:
    """
    Valve configuration parameters.
    
    Parameter validation is handled by the firmware. The client passes
    values through to the REST API without enforcing ranges.
    
    Attributes:
        viscous: Viscous damping coefficient [N·m·s/rad]
        coulomb: Coulomb friction torque [N·m]
        wall_stiffness: Virtual wall stiffness [N·m/turn]
        wall_damping: Virtual wall damping [N·m·s/turn]
        smoothing: Friction smoothing epsilon
        torque_limit: Maximum torque [N·m]
        open_position: Fully open position [degrees]
        closed_position: Fully closed position [degrees]
        degrees_per_turn: Mechanical scaling [deg/turn]
    """

    viscous: float = 0.05
    coulomb: float = 0.01
    wall_stiffness: float = 1.0
    wall_damping: float = 0.1
    smoothing: float = 0.001
    torque_limit: float = 0.5
    open_position: float = 90.0
    closed_position: float = 0.0
    degrees_per_turn: float = 360.0

    def __post_init__(self) -> None:
        """Validate parameters after initialization."""
        # Parameter range validation is handled by the firmware.
        # The client simply passes values through to the REST API.
        pass

    def to_dict(self) -> dict:
        """Convert to dictionary for API requests."""
        return {
            "viscous": self.viscous,
            "coulomb": self.coulomb,
            "wall_stiffness": self.wall_stiffness,
            "wall_damping": self.wall_damping,
            "smoothing": self.smoothing,
            "torque_limit": self.torque_limit,
            "open_position": self.open_position,
            "closed_position": self.closed_position,
            "degrees_per_turn": self.degrees_per_turn,
        }

    @classmethod
    def from_dict(cls, data: dict) -> "ValveConfig":
        """Create ValveConfig from API response dictionary."""
        return cls(
            viscous=data.get("viscous", 0.05),
            coulomb=data.get("coulomb", 0.01),
            wall_stiffness=data.get("wall_stiffness", 1.0),
            wall_damping=data.get("wall_damping", 0.1),
            smoothing=data.get("smoothing", 0.001),
            torque_limit=data.get("torque_limit", 0.5),
            open_position=data.get("open_position", 90.0),
            closed_position=data.get("closed_position", 0.0),
            degrees_per_turn=data.get("degrees_per_turn", 360.0),
        )


@dataclass
class PresetConfig:
    """
    Preset configuration parameters.
    
    Presets are stored in STEVE firmware's non-volatile memory and can be
    loaded to quickly switch between different valve configurations.
    
    Attributes:
        index: Preset slot (0-3)
        name: Preset name (max 15 characters)
        viscous: Viscous damping coefficient [N·m·s/rad]
        coulomb: Coulomb friction torque [N·m]
        wall_stiffness: Virtual wall stiffness [N·m/turn]
        wall_damping: Virtual wall damping [N·m·s/turn]
        travel: Total angular travel [degrees] (1-360)
        torque_limit: Maximum torque [N·m]
        smoothing: Friction smoothing epsilon
        theta_closed: Closed position offset from zero reference [degrees]
        theta_open: Open position offset from zero reference [degrees]
    """

    index: int
    name: str = "custom"
    viscous: float = 0.05
    coulomb: float = 0.01
    wall_stiffness: float = 1.0
    wall_damping: float = 0.1
    travel: float = 90.0
    torque_limit: float = 0.5
    smoothing: float = 0.001
    theta_closed: float = 0.0
    theta_open: float = 90.0

    def __post_init__(self) -> None:
        """Validate parameters after initialization."""
        from pysteve.core.exceptions import SteveValidationError

        if not (0 <= self.index <= 3):
            raise SteveValidationError(f"index must be between 0 and 3, got {self.index}")

        if len(self.name) > 15:
            raise SteveValidationError(
                f"name must be 15 characters or less, got {len(self.name)}"
            )

        # Other parameter range validation is handled by the firmware.

    def to_dict(self) -> dict:
        """Convert to dictionary for API requests."""
        return {
            "index": self.index,
            "name": self.name,
            "viscous": self.viscous,
            "coulomb": self.coulomb,
            "wall_stiffness": self.wall_stiffness,
            "wall_damping": self.wall_damping,
            "travel": self.travel,
            "torque_limit": self.torque_limit,
            "smoothing": self.smoothing,
            "theta_closed": self.theta_closed,
            "theta_open": self.theta_open,
        }

    @classmethod
    def from_dict(cls, data: dict) -> "PresetConfig":
        """Create PresetConfig from API response dictionary."""
        return cls(
            index=data["index"],
            name=data.get("name", "custom"),
            viscous=data.get("viscous", 0.05),
            coulomb=data.get("coulomb", 0.01),
            wall_stiffness=data.get("wall_stiffness", 1.0),
            wall_damping=data.get("wall_damping", 0.1),
            travel=data.get("travel", 90.0),
            torque_limit=data.get("torque_limit", 0.5),
            smoothing=data.get("smoothing", 0.001),
            theta_closed=data.get("theta_closed", 0.0),
            theta_open=data.get("theta_open", 90.0),
        )
