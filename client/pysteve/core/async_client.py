"""
Asynchronous REST API client for concurrent multi-device control.
"""

import asyncio
from typing import Optional, Callable, Dict, Any, List

try:
    import aiohttp
except ImportError:
    raise ImportError(
        "aiohttp is required for async client. "
        "Install with: pip install pysteve[async]"
    )

from pysteve.core.config import ValveConfig, PresetConfig
from pysteve.core.exceptions import (
    SteveConnectionError,
    SteveAPIError,
    SteveTimeoutError,
    SteveAuthError,
    SteveValidationError,
)


class SteveAsyncClient:
    """
    Asynchronous client for STEVE haptic valve system.
    
    Provides async REST API access for concurrent control of multiple devices.
    Useful for PhD students and advanced users doing multi-device coordination.
    
    Args:
        host: STEVE device IP address or hostname
        port: HTTP API port (default: 8080)
        api_key: API authentication key (default: "steve-valve-2025")
        timeout: Request timeout in seconds (default: 5.0)
        auto_reconnect: Enable automatic reconnection (default: True)
        max_retries: Maximum reconnection attempts (default: 10)
    
    Example:
        >>> async with SteveAsyncClient("192.168.1.100") as steve:
        ...     await steve.enable_motor()
        ...     await steve.start_valve()
        ...     status = await steve.get_status()
        ...     print(f"Position: {status['pos_deg']:.2f}°")
    """

    def __init__(
        self,
        host: str,
        port: int = 8080,
        api_key: str = "steve-valve-2025",
        timeout: float = 5.0,
        auto_reconnect: bool = True,
        max_retries: int = 10,
    ):
        self.host = host
        self.port = port
        self.api_key = api_key
        self.timeout = timeout
        self.auto_reconnect = auto_reconnect
        self.max_retries = max_retries

        self.base_url = f"http://{host}:{port}/api/v1"
        self._session: Optional[aiohttp.ClientSession] = None
        self._connected = False

    async def __aenter__(self) -> "SteveAsyncClient":
        """Async context manager entry."""
        await self.connect()
        return self

    async def __aexit__(self, exc_type, exc_val, exc_tb) -> None:
        """Async context manager exit."""
        await self.disconnect()

    async def connect(self) -> None:
        """Establish connection to STEVE device."""
        if self._session is not None:
            return

        headers = {"X-API-Key": self.api_key}
        timeout = aiohttp.ClientTimeout(total=self.timeout)
        self._session = aiohttp.ClientSession(headers=headers, timeout=timeout)

        # Test connection
        try:
            async with self._session.get(f"{self.base_url}/status") as response:
                if response.status == 401:
                    raise SteveAuthError("Invalid API key")
                response.raise_for_status()
                self._connected = True
        except asyncio.TimeoutError:
            raise SteveTimeoutError(f"Connection timeout to {self.host}:{self.port}")
        except aiohttp.ClientError as e:
            raise SteveConnectionError(f"Failed to connect: {e}")

    async def disconnect(self) -> None:
        """Close connection to STEVE device."""
        if self._session:
            await self._session.close()
            self._session = None
        self._connected = False

    async def _request(
        self,
        method: str,
        endpoint: str,
        json: Optional[Dict[str, Any]] = None,
        params: Optional[Dict[str, Any]] = None,
    ) -> Dict[str, Any]:
        """Make async HTTP request."""
        if not self._session:
            raise SteveConnectionError("Not connected. Call connect() first.")

        url = f"{self.base_url}/{endpoint}"

        try:
            async with self._session.request(
                method, url, json=json, params=params
            ) as response:
                if response.status == 401:
                    raise SteveAuthError("Invalid API key")

                if response.status >= 400:
                    try:
                        error_data = await response.json()
                        error_msg = error_data.get("error", "Unknown error")
                    except Exception:
                        error_msg = await response.text() or "Unknown error"

                    raise SteveAPIError(
                        f"API error: {error_msg}",
                        status_code=response.status,
                        response_data=error_data if "error_data" in locals() else None,
                    )

                return await response.json() if response.content_length else {}

        except asyncio.TimeoutError:
            raise SteveTimeoutError(f"Request timeout: {url}")
        except aiohttp.ClientError as e:
            raise SteveConnectionError(f"Request failed: {e}")

    # Valve Control Methods (async versions)

    async def start_valve(self, preset: Optional[int] = None) -> Dict[str, Any]:
        """
        Start valve control with optional preset.
        
        Args:
            preset: Preset index (0-3) to load (optional)
        """
        data = {"action": "start"}
        if preset is not None:
            if not isinstance(preset, int) or preset < 0 or preset > 3:
                raise SteveValidationError("preset must be an integer from 0 to 3")
            data["preset"] = preset
        return await self._request("POST", "control", json=data)

    async def stop_valve(self) -> Dict[str, Any]:
        """Stop valve control."""
        return await self._request("POST", "control", json={"action": "stop"})

    async def get_status(self) -> Dict[str, Any]:
        """Get real-time valve status."""
        return await self._request("GET", "status")

    async def get_config(self) -> ValveConfig:
        """Get current valve configuration."""
        data = await self._request("GET", "config")
        return ValveConfig.from_dict(data)

    async def update_config(self, **kwargs) -> Dict[str, Any]:
        """Update valve configuration parameters."""
        valid_params = {
            "viscous",
            "coulomb",
            "wall_stiffness",
            "wall_damping",
            "smoothing",
            "torque_limit",
            "open_position",
            "closed_position",
            "degrees_per_turn",
        }
        invalid = set(kwargs.keys()) - valid_params
        if invalid:
            raise SteveValidationError(f"Invalid parameters: {invalid}")

        return await self._request("POST", "config", json=kwargs)

    async def get_presets(self) -> List[PresetConfig]:
        """Get all available presets."""
        data = await self._request("GET", "presets")
        return [PresetConfig.from_dict(p) for p in data]

    async def load_preset(self, preset: int) -> Dict[str, Any]:
        """
        Load a preset configuration.
        
        Args:
            preset: Preset index (0-3)
        
        Returns:
            Response data
        """
        if not isinstance(preset, int) or preset < 0 or preset > 3:
            raise SteveValidationError("preset must be an integer from 0 to 3")
        data = {"preset": preset}
        return await self._request("POST", "control", json=data)

    async def save_preset(
        self, index: int, save_current: bool = True, preset: Optional[PresetConfig] = None
    ) -> Dict[str, Any]:
        """Save preset configuration."""
        if save_current:
            data = {"index": index, "save_current": True}
        else:
            if preset is None:
                raise SteveValidationError("preset required when save_current=False")
            data = preset.to_dict()
        return await self._request("POST", "presets", json=data)

    async def enable_motor(self) -> Dict[str, Any]:
        """Enable ODrive motor controller."""
        return await self._request("POST", "odrive", json={"action": "enable"})

    async def disable_motor(self) -> Dict[str, Any]:
        """Disable ODrive motor controller."""
        return await self._request("POST", "odrive", json={"action": "disable"})

    async def calibrate_motor(self) -> Dict[str, Any]:
        """Calibrate ODrive motor and encoder."""
        return await self._request("POST", "odrive", json={"action": "calibrate"})

    async def get_odrive_status(self) -> Dict[str, Any]:
        """Get ODrive motor controller status."""
        return await self._request("GET", "odrive")

    async def set_torque(self, torque: float) -> Dict[str, Any]:
        """Set motor torque directly (bypass valve control)."""
        return await self._request("POST", "odrive", json={"action": "set_torque", "value": torque})

    async def set_position(self, position: float) -> Dict[str, Any]:
        """Set motor position (requires position control mode)."""
        return await self._request("POST", "odrive", json={"action": "set_position", "value": position})

    async def set_velocity(self, velocity: float) -> Dict[str, Any]:
        """Set motor velocity (requires velocity control mode)."""
        return await self._request("POST", "odrive", json={"action": "set_velocity", "value": velocity})

    async def set_odrive_mode(self, mode: int) -> Dict[str, Any]:
        """Set ODrive controller mode (0=voltage, 1=torque, 2=velocity, 3=position)."""
        if mode < 0 or mode > 3:
            raise SteveValidationError("mode must be 0-3")
        return await self._request("POST", "odrive", json={"action": "set_mode", "value": mode})

    @property
    def is_connected(self) -> bool:
        """Check if client is connected."""
        return self._connected
