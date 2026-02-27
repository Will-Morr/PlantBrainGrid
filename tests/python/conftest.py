"""Pytest configuration for PlantBrainGrid Python tests."""

import sys
import os

# Add project paths so both the package and the .so are findable
_PROJECT_ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "../.."))
_PYTHON_PKG = os.path.join(_PROJECT_ROOT, "src", "python")
_BUILD_DIR = os.path.join(_PROJECT_ROOT, "build")

for path in [_PROJECT_ROOT, _PYTHON_PKG, _BUILD_DIR]:
    if path not in sys.path:
        sys.path.insert(0, path)


def pytest_configure(config):
    config.addinivalue_line("markers", "requires_bindings: mark test as requiring C++ bindings")


def pytest_collection_modifyitems(config, items):
    """Skip tests that require bindings if they aren't available."""
    try:
        import _plantbraingrid  # noqa: F401
    except ImportError:
        import pytest
        skip_no_bindings = pytest.mark.skip(reason="C++ bindings not built")
        for item in items:
            if "requires_bindings" in item.keywords:
                item.add_marker(skip_no_bindings)
