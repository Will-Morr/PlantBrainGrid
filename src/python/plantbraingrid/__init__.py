"""PlantBrainGrid - Plant evolution simulation with bytecode brains."""

__version__ = "0.1.0"

# Try to import C++ bindings
try:
    from _plantbraingrid import *
except ImportError:
    pass
