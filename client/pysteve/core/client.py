"""
Synchronous REST API client for STEVE valve control.
"""

import time
from typing import Optional, Callable, Dict, Any, List
import requests
from requests.adapters import HTTPAdapter
from urllib3.util.retry import Retry

from pysteve.core.config import ValveConfig, PresetConfig
from pysteve.core.exceptions import (
    SteveConnectionError,
    SteveAPIError,
    SteveTimeoutError,
    SteveAuthError,
    SteveValidationError,
)


class SteveClient:
    """
    Synchronous client for STEVE haptic valve system.
    
    Provides REST API access to valve control, configuration, monitoring,
    and preset management with automatic reconnection and error handling.
    
    Args:
        host: STEVE device IP address or hostname
        port: HTTP API port (default: 8080)
        api_key: API authentication key (default: "steve-valve-2025")
        timeout: Request timeout in seconds (default: 5.0)
        auto_reconnect: Enable automatic reconnection on failure (default: True)
        max_retries: Maximum reconnection attempts (default: 10)
        backoff_initial: Initial backoff delay in seconds (default: 1.0)
        backoff_max: Maximum backoff delay in seconds (default: 30.0)
        on_disconnect: Callback function called when connection is lost
        on_reconnect: Callback function called when connection is restored
    
    Example:
        >>> with SteveClient("192.168.1.100") as steve:
        ...     steve.enable_motor()
        ...     steve.start_valve()
        ...     status = steve.get_status()
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
        backoff_initial: float = 1.0,
        backoff_max: float = 30.0,
        on_disconnect: Optional[Callable[[], None]] = None,
        on_reconnect: Optional[Callable[[], None]] = None,
    ):
        self.host = host
        self.port = port
        self.api_key = api_key
        self.timeout = timeout
        self.auto_reconnect = auto_reconnect
        self.max_retries = max_retries
        self.backoff_initial = backoff_initial
        self.backoff_max = backoff_max
        self.on_disconnect = on_disconnect
        self.on_reconnect = on_reconnect

        self.base_url = f"http://{host}:{port}/api/v1"
        self._session: Optional[requests.Session] = None
        self._connected = False
        self._retry_count = 0

    def __enter__(self) -> "SteveClient":
        """Context manager entry."""
        self.connect()
        return self

    def __exit__(self, exc_type, exc_val, exc_tb) -> None:
        """Context manager exit."""
        self.disconnect()

    def connect(self) -> None:
        """
        Establish connection to STEVE device.
        
        Raises:
            SteveConnectionError: If connection fails
        """
        if self._session is not None:
            return

        self._session = requests.Session()
        self._session.headers.update({"X-API-Key": self.api_key})

        # Configure retry strategy for resilience
        retry_strategy = Retry(
            total=3,
            backoff_factor=0.5,
            status_forcelist=[429, 500, 502, 503, 504],
            allowed_methods=["HEAD", "GET", "OPTIONS", "POST"],
        )
        adapter = HTTPAdapter(max_retries=retry_strategy)
        self._session.mount("http://", adapter)

        # Test connection
        try:
            response = self._session.get(
                f"{self.base_url}/status", timeout=self.timeout
            )
            if response.status_code == 401:
                raise SteveAuthError("Invalid API key")
            response.raise_for_status()
            self._connected = True
            self._retry_count = 0
        except requests.exceptions.Timeout:
            raise SteveTimeoutError(
                f"Connection timeout to {self.host}:{self.port}"
            )
        except requests.exceptions.ConnectionError as e:
            raise SteveConnectionError(
                f"Failed to connect to {self.host}:{self.port}: {e}"
            )
        except requests.exceptions.RequestException as e:
            raise SteveConnectionError(f"Connection failed: {e}")

    def disconnect(self) -> None:
        """Close connection to STEVE device."""
        if self._session:
            self._session.close()
            self._session = None
        self._connected = False

    def _request(
        self,
        method: str,
        endpoint: str,
        json: Optional[Dict[str, Any]] = None,
        params: Optional[Dict[str, Any]] = None,
    ) -> Dict[str, Any]:
        """
        Make HTTP request with automatic reconnection.
        
        Args:
            method: HTTP method (GET, POST, etc.)
            endpoint: API endpoint path
            json: JSON request body
            params: URL query parameters
        
        Returns:
            JSON response data
        
        Raises:
            SteveConnectionError: If connection fails
            SteveAPIError: If API returns error response
            SteveTimeoutError: If request times out
        """
        if not self._session:
            raise SteveConnectionError("Not connected. Call connect() first.")

        url = f"{self.base_url}/{endpoint}"
        backoff = self.backoff_initial

        while True:
            try:
                response = self._session.request(
                    method, url, json=json, params=params, timeout=self.timeout
                )

                if response.status_code == 401:
                    raise SteveAuthError("Invalid API key")

                if response.status_code >= 400:
                    try:
                        error_data = response.json()
                        error_msg = error_data.get("error", "Unknown error")
                    except Exception:
                        error_msg = response.text or "Unknown error"

                    raise SteveAPIError(
                        f"API error: {error_msg}",
                        status_code=response.status_code,
                        response_data=error_data if "error_data" in locals() else None,
                    )

                # Successful request - reset retry count
                if self._retry_count > 0:
                    self._retry_count = 0
                    if self.on_reconnect:
                        self.on_reconnect()

                return response.json() if response.content else {}

            except requests.exceptions.Timeout:
                if not self.auto_reconnect or self._retry_count >= self.max_retries:
                    raise SteveTimeoutError(f"Request timeout: {url}")

            except (requests.exceptions.ConnectionError, requests.exceptions.RequestException) as e:
                if not self.auto_reconnect or self._retry_count >= self.max_retries:
                    raise SteveConnectionError(f"Connection lost: {e}")

            # Handle reconnection
            self._connected = False
            self._retry_count += 1

            if self._retry_count == 1 and self.on_disconnect:
                self.on_disconnect()

            if self._retry_count > self.max_retries:
                raise SteveConnectionError(
                    f"Max retries ({self.max_retries}) exceeded"
                )

            # Exponential backoff
            time.sleep(backoff)
            backoff = min(backoff * 2, self.backoff_max)

            # Attempt reconnection
            try:
                self.disconnect()
                self.connect()
            except Exception:
                pass  # Will retry on next iteration

    # Valve Control Methods

    def start_valve(self, preset: Optional[int] = None) -> Dict[str, Any]:
        """
        Start valve control with optional preset.
        
        Args:
            preset: Preset index (0-3) to load (optional)
        
        Returns:
            Response data containing mode and status
        
        Example:
            >>> steve.start_valve()
            >>> steve.start_valve(0)  # Start with light preset
            >>> steve.start_valve(1)  # Start with medium preset
        """
        data = {"action": "start"}
        if preset is not None:
            if not isinstance(preset, int) or preset < 0 or preset > 3:
                raise SteveValidationError("preset must be an integer from 0 to 3")
            data["preset"] = preset
        return self._request("POST", "control", json=data)

    def stop_valve(self) -> Dict[str, Any]:
        """
        Stop valve control.
        
        Returns:
            Response data containing status
        """
        return self._request("POST", "control", json={"action": "stop"})

    def get_status(self) -> Dict[str, Any]:
        """
        Get real-time valve status and measurements.
        
        Returns:
            Dictionary containing:
                - mode: "idle" or "running"
                - pos_deg: Current position [degrees]
                - vel_rad_s: Current velocity [rad/s]
                - torque_nm: Applied torque [N·m]
                - energy_j: Passivity energy [J]
                - loop_hz: Control loop frequency [Hz]
                - pos_raw: Raw encoder position
                - vel_raw: Raw encoder velocity
        
        Example:
            >>> status = steve.get_status()
            >>> print(f"Position: {status['pos_deg']:.2f}°")
        """
        return self._request("GET", "status")

    # Configuration Methods

    def get_config(self) -> ValveConfig:
        """
        Get current valve configuration.
        
        Returns:
            ValveConfig object with current parameters
        """
        data = self._request("GET", "config")
        return ValveConfig.from_dict(data)

    def update_config(self, **kwargs) -> Dict[str, Any]:
        """
        Update valve configuration parameters.
        
        Only provided parameters are updated. Changes take effect immediately
        without stopping the valve.
        
        Args:
            **kwargs: Configuration parameters to update
                viscous: Viscous damping [N·m·s/rad]
                coulomb: Coulomb friction [N·m]
                wall_stiffness: Wall stiffness [N·m/turn]
                wall_damping: Wall damping [N·m·s/turn]
                smoothing: Smoothing epsilon
                torque_limit: Torque limit [N·m]
                open_position: Open position [degrees]
                closed_position: Closed position [degrees]
                degrees_per_turn: Mechanical scaling [deg/turn]
        
        Returns:
            Response data with number of fields updated
        
        Example:
            >>> steve.update_config(viscous=0.08, coulomb=0.015)
        """
        # Validate parameters
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

        return self._request("POST", "config", json=kwargs)

    # Preset Methods

    def get_presets(self) -> List[PresetConfig]:
        """
        Get all available presets.
        
        Returns:
            List of PresetConfig objects for all 4 preset slots
        """
        data = self._request("GET", "presets")
        return [PresetConfig.from_dict(p) for p in data]

    def load_preset(self, preset: int) -> Dict[str, Any]:
        """
        Load a preset configuration.
        
        Args:
            preset: Preset index (0-3)
        
        Returns:
            Response data
        
        Example:
            >>> steve.load_preset(0)  # Load first preset
            >>> steve.load_preset(1)  # Load second preset
        """
        if not isinstance(preset, int) or preset < 0 or preset > 3:
            raise SteveValidationError("preset must be an integer from 0 to 3")
        data = {"preset": preset}
        return self._request("POST", "control", json=data)

    def save_preset(
        self, index: int, save_current: bool = True, preset: Optional[PresetConfig] = None
    ) -> Dict[str, Any]:
        """
        Save preset configuration.
        
        Args:
            index: Preset slot (0-3)
            save_current: If True, save current valve config to preset
            preset: If save_current=False, save this preset configuration
        
        Returns:
            Response data
        
        Example:
            >>> # Save current config to preset 0
            >>> steve.save_preset(0, save_current=True)
            
            >>> # Save specific preset config
            >>> preset = PresetConfig(index=1, name="custom", viscous=0.06)
            >>> steve.save_preset(1, save_current=False, preset=preset)
        """
        if save_current:
            data = {"index": index, "save_current": True}
        else:
            if preset is None:
                raise SteveValidationError(
                    "preset parameter required when save_current=False"
                )
            data = preset.to_dict()

        return self._request("POST", "presets", json=data)

    # ODrive Motor Control Methods

    def enable_motor(self) -> Dict[str, Any]:
        """
        Enable ODrive motor controller.
        
        Returns:
            Response data
        """
        return self._request("POST", "odrive", json={"action": "enable"})

    def disable_motor(self) -> Dict[str, Any]:
        """
        Disable ODrive motor controller.
        
        Returns:
            Response data
        """
        return self._request("POST", "odrive", json={"action": "disable"})

    def calibrate_motor(self) -> Dict[str, Any]:
        """
        Calibrate ODrive motor and encoder.
        
        This may take several seconds to complete.
        
        Returns:
            Response data
        """
        return self._request("POST", "odrive", json={"action": "calibrate"})

    def estop_motor(self) -> Dict[str, Any]:
        """
        Emergency stop - immediately disable motor.
        
        Returns:
            Response data
        """
        return self._request("POST", "odrive", json={"action": "estop"})

    def get_odrive_status(self) -> Dict[str, Any]:
        """
        Get ODrive motor controller status.
        
        Returns:
            Dictionary containing motor state, errors, telemetry
        """
        return self._request("GET", "odrive")

    def set_torque(self, torque: float) -> Dict[str, Any]:
        """
        Set motor torque directly (bypass valve control).
        
        Args:
            torque: Torque in N·m
        
        Returns:
            Response data
        """
        return self._request("POST", "odrive", json={"action": "set_torque", "value": torque})

    # Streaming Control

    def start_stream(self, interval_ms: int = 100) -> Dict[str, Any]:
        """
        Start TCP data streaming.
        
        Args:
            interval_ms: Streaming interval in milliseconds (10-1000)
        
        Returns:
            Response data with streaming status
        
        Note:
            Connect to TCP port 8888 to receive streamed data.
            Use SteveStreamer class for convenient streaming access.
        """
        return self._request("POST", "stream", json={"action": "start", "interval_ms": interval_ms})

    def stop_stream(self) -> Dict[str, Any]:
        """
        Stop TCP data streaming.
        
        Returns:
            Response data
        """
        return self._request("POST", "stream", json={"action": "stop"})

    def get_stream_status(self) -> Dict[str, Any]:
        """
        Get streaming server status.
        
        Returns:
            Dictionary containing streaming state and statistics
        """
        return self._request("GET", "stream")

    # Performance and Diagnostics

    def get_performance(self) -> Dict[str, Any]:
        """
        Get detailed performance statistics.
        
        Returns:
            Dictionary containing motion stats, timing, and safety events
        """
        return self._request("GET", "performance")

    def get_can_status(self) -> Dict[str, Any]:
        """
        Get CAN bus status and statistics.
        
        Returns:
            Dictionary containing CAN communication status
        """
        return self._request("GET", "can")

    # Calibration Methods

    def get_calibration(self) -> Dict[str, Any]:
        """
        Get absolute position calibration status.
        
        Returns:
            Dictionary containing:
                - calibrated: Whether zero reference has been set
                - valid: Whether calibration appears undisturbed
                - uses_absolute_ref: Whether absolute reference is active
                - zero_reference: Zero reference details (turns, validation)
                - current_position: Current encoder and absolute position
        
        Example:
            >>> cal = steve.get_calibration()
            >>> if cal['calibrated']:
            ...     print(f"Absolute position: {cal['current_position']['absolute_position_deg']:.1f}°")
        """
        return self._request("GET", "calibration")

    def set_zero_here(self) -> Dict[str, Any]:
        """
        Set current position as the absolute zero reference.
        
        The current encoder position is stored in NVM and used as the
        reference for all future position measurements. This setting
        persists across power cycles.
        
        The valve must be running to calibrate.
        
        Returns:
            Dictionary containing zero_reference value
        
        Example:
            >>> steve.start_valve()
            >>> # Move shaft to desired zero position
            >>> steve.set_zero_here()
            >>> print("Zero calibrated at current position")
        """
        return self._request("POST", "calibration", json={"action": "set_zero"})

    def set_zero_at(self, encoder_turns: float) -> Dict[str, Any]:
        """
        Set zero reference to a specific encoder value.
        
        Useful for restoring a known calibration without having to
        physically position the shaft.
        
        Args:
            encoder_turns: Raw encoder position in turns to use as zero
        
        Returns:
            Response data
        
        Example:
            >>> # Restore previously known calibration
            >>> steve.set_zero_at(0.234567)
        """
        return self._request("POST", "calibration", json={
            "action": "set_zero_at",
            "value": encoder_turns
        })

    def validate_calibration(self) -> bool:
        """
        Check if calibration is still valid.
        
        Validates that the magnetic encoder hasn't been disturbed since
        calibration. Returns True if the encoder reading is consistent
        with the stored calibration reference.
        
        Returns:
            True if calibration is valid, False if disturbed or uncalibrated
        
        Example:
            >>> if steve.validate_calibration():
            ...     print("Calibration OK")
            ... else:
            ...     print("WARNING: Calibration may be invalid")
        """
        result = self._request("POST", "calibration", json={"action": "validate"})
        return result.get("valid", False)

    def clear_calibration(self) -> Dict[str, Any]:
        """
        Clear all calibration data.
        
        Removes the stored zero reference.
        After clearing, the system returns to relative positioning mode.
        
        Returns:
            Response data
        
        Example:
            >>> steve.clear_calibration()
            >>> print("Calibration cleared")
        """
        return self._request("DELETE", "calibration")

    def get_absolute_position(self) -> Optional[float]:
        """
        Get current absolute position in degrees.
        
        Returns the position relative to the calibrated zero reference.
        Returns None if not calibrated.
        
        Returns:
            Position in degrees (0-360), or None if not calibrated
        
        Example:
            >>> pos = steve.get_absolute_position()
            >>> if pos is not None:
            ...     print(f"Absolute position: {pos:.1f}°")
        """
        cal = self.get_calibration()
        if not cal.get("calibrated", False):
            return None
        return cal.get("current_position", {}).get("absolute_position_deg")

    # Utility Methods

    @property
    def is_connected(self) -> bool:
        """Check if client is connected to STEVE device."""
        return self._connected

    def ping(self) -> bool:
        """
        Ping STEVE device to check connectivity.
        
        Returns:
            True if device responds, False otherwise
        """
        try:
            self.get_status()
            return True
        except Exception:
            return False
