#!/usr/bin/env python3
"""
check_defines.py - Detect duplicate #define symbols across source files.

This script scans C source and header files for #define macros and reports
any duplicates that may cause configuration inconsistencies.

Usage:
    python3 scripts/check_defines.py [--verbose]

Exit codes:
    0 - No duplicate defines found
    1 - Duplicate defines detected
"""

import os
import re
import sys
import argparse
from collections import defaultdict
from pathlib import Path

# Directories to scan (relative to firmware root)
SCAN_DIRS = ['src', 'inc', 'common', 'ports']

# Directories to exclude (third-party code, build artifacts)
EXCLUDE_DIRS = {'third_party', 'build', '.git'}

# File extensions to scan
EXTENSIONS = {'.c', '.h'}

# Defines to ignore (standard/expected duplicates)
IGNORE_DEFINES = {
    # Standard include guards
    'BOARD_CONFIG_H',
    'VALVE_CONFIG_H',
    'NVM_CONFIG_H',
    'NETWORK_CONFIG_H',
    'CONFIG_H',
    # HAL/CMSIS standard defines
    'HSE_VALUE',
    'LSE_VALUE',
    '__FPU_PRESENT',
    '__DSP_PRESENT',
    # HAL Ethernet descriptor counts (same value, different literal 4 vs 4U)
    'ETH_RX_DESC_CNT',
    'ETH_TX_DESC_CNT',
    # Include guard pattern
}

# Pattern to match #define statements
DEFINE_PATTERN = re.compile(
    r'^\s*#\s*define\s+([A-Za-z_][A-Za-z0-9_]*)\s*(.*)?$',
    re.MULTILINE
)

# Pattern to detect include guards (ends with _H or _H_)
INCLUDE_GUARD_PATTERN = re.compile(r'^[A-Z_]+_H_?$')


def is_include_guard(name: str) -> bool:
    """Check if a define name looks like an include guard."""
    return bool(INCLUDE_GUARD_PATTERN.match(name))


def find_defines(filepath: Path) -> list[tuple[str, str, int]]:
    """
    Find all #define statements in a file.
    
    Returns list of (name, value, line_number) tuples.
    """
    defines = []
    try:
        with open(filepath, 'r', encoding='utf-8', errors='replace') as f:
            content = f.read()
            for line_num, line in enumerate(content.split('\n'), 1):
                match = DEFINE_PATTERN.match(line)
                if match:
                    name = match.group(1)
                    value = match.group(2).strip() if match.group(2) else ''
                    # Remove trailing comments
                    if '//' in value:
                        value = value.split('//')[0].strip()
                    if '/*' in value:
                        value = value.split('/*')[0].strip()
                    defines.append((name, value, line_num))
    except Exception as e:
        print(f"Warning: Could not read {filepath}: {e}", file=sys.stderr)
    return defines


def scan_directory(base_path: Path, verbose: bool = False) -> dict[str, list[tuple[Path, str, int]]]:
    """
    Scan directories for #define statements.
    
    Returns dict mapping define names to list of (filepath, value, line) tuples.
    """
    defines_map = defaultdict(list)
    
    for scan_dir in SCAN_DIRS:
        dir_path = base_path / scan_dir
        if not dir_path.exists():
            continue
            
        for root, dirs, files in os.walk(dir_path):
            # Remove excluded directories from search
            dirs[:] = [d for d in dirs if d not in EXCLUDE_DIRS]
            
            for filename in files:
                if Path(filename).suffix not in EXTENSIONS:
                    continue
                    
                filepath = Path(root) / filename
                if verbose:
                    print(f"Scanning: {filepath.relative_to(base_path)}")
                
                for name, value, line_num in find_defines(filepath):
                    defines_map[name].append((filepath, value, line_num))
    
    return defines_map


def check_duplicates(defines_map: dict, base_path: Path, verbose: bool = False) -> list[tuple[str, list]]:
    """
    Check for duplicate defines with different values.
    
    Returns list of (define_name, occurrences) tuples for duplicates.
    """
    duplicates = []
    
    for name, occurrences in sorted(defines_map.items()):
        # Skip if only one occurrence
        if len(occurrences) <= 1:
            continue
            
        # Skip ignored defines and include guards
        if name in IGNORE_DEFINES or is_include_guard(name):
            continue
        
        # Skip if all occurrences are in the same file (intentional #ifdef redefinitions)
        unique_files = set(occ[0] for occ in occurrences)
        if len(unique_files) == 1:
            continue
        
        # Check if all values are the same
        values = set(occ[1] for occ in occurrences)
        if len(values) == 1:
            # Same value, might be intentional (e.g., same header included)
            # Still report if in different source files (not headers)
            source_files = [occ[0] for occ in occurrences if occ[0].suffix == '.c']
            if len(source_files) > 1:
                duplicates.append((name, occurrences))
            elif verbose:
                print(f"Info: {name} defined {len(occurrences)} times with same value")
        else:
            # Different values across different files - definitely a problem
            duplicates.append((name, occurrences))
    
    return duplicates


def main():
    parser = argparse.ArgumentParser(
        description='Check for duplicate #define symbols in C source files'
    )
    parser.add_argument(
        '--verbose', '-v',
        action='store_true',
        help='Show detailed scanning progress'
    )
    parser.add_argument(
        '--base-path',
        type=Path,
        default=Path(__file__).parent.parent,
        help='Base path of firmware directory'
    )
    args = parser.parse_args()
    
    base_path = args.base_path.resolve()
    print(f"Scanning for duplicate defines in: {base_path}")
    print()
    
    # Scan all files
    defines_map = scan_directory(base_path, args.verbose)
    print(f"Found {len(defines_map)} unique define symbols")
    
    # Check for duplicates
    duplicates = check_duplicates(defines_map, base_path, args.verbose)
    
    if not duplicates:
        print()
        print("✓ No problematic duplicate defines found")
        return 0
    
    # Report duplicates
    print()
    print(f"✗ Found {len(duplicates)} duplicate define(s):")
    print()
    
    for name, occurrences in duplicates:
        values = set(occ[1] for occ in occurrences)
        if len(values) > 1:
            print(f"  {name} (CONFLICTING VALUES):")
        else:
            print(f"  {name} (duplicated in source files):")
        
        for filepath, value, line_num in occurrences:
            rel_path = filepath.relative_to(base_path)
            display_value = value[:50] + '...' if len(value) > 50 else value
            print(f"    {rel_path}:{line_num}: {display_value or '(no value)'}")
        print()
    
    return 1


if __name__ == '__main__':
    sys.exit(main())
