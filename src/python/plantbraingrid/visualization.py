"""Visualization module using raylib."""

import os
from dataclasses import dataclass
from typing import Optional, Tuple
import math

_MONO_FONT_CANDIDATES = [
    # "/usr/share/fonts/truetype/ubuntu/UbuntuMono-R.ttf",
    # "/usr/share/fonts/truetype/liberation/LiberationMono-Regular.ttf",
    "/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf",
    "/usr/share/fonts/truetype/freefont/FreeMono.ttf",
]

try:
    import pyray as rl
    RAYLIB_AVAILABLE = True
except ImportError:
    RAYLIB_AVAILABLE = False

# Cell type colors (RGB tuples)
CELL_COLORS = {
    0: (50, 50, 50),      # Empty
    1: (139, 69, 19),     # Primary (brown)
    2: (34, 139, 34),     # SmallLeaf (green)
    3: (0, 100, 0),       # BigLeaf (dark green)
    4: (139, 90, 43),     # FiberRoot (sienna)
    5: (210, 180, 140),   # Xylem (tan)
    6: (255, 215, 0),     # FireproofXylem (gold)
    7: (128, 128, 128),   # Thorn (gray)
    8: (255, 69, 0),      # FireStarter (red-orange)
    9: (110, 55, 90),     # TapRoot (purplish brown)
}

WATER_COLOR = (0, 100, 200, 100)
NUTRIENT_COLOR = (139, 69, 19, 100)
FIRE_COLOR = (255, 100, 0, 200)
SELECTION_COLOR = (255, 255, 0)


@dataclass
class Camera:
    """2D camera for pan/zoom.

    World coordinates are integers (grid cells).
    Screen coordinates are pixels.
    Relationship:  screen = (world - camera) * zoom * cell_size
    """
    x: float = 0.0
    y: float = 0.0
    zoom: float = 1.0
    cell_size: int = 8  # pixels per world cell at zoom=1

    def world_to_screen(self, world_x: float, world_y: float) -> Tuple[float, float]:
        """Convert world grid coordinates to screen pixel coordinates."""
        scale = self.zoom * self.cell_size
        screen_x = (world_x - self.x) * scale
        screen_y = (world_y - self.y) * scale
        return screen_x, screen_y

    def screen_to_world(self, screen_x: float, screen_y: float) -> Tuple[int, int]:
        """Convert screen pixel coordinates to world grid coordinates (integer)."""
        scale = self.zoom * self.cell_size
        world_x = int(screen_x / scale + self.x)
        world_y = int(screen_y / scale + self.y)
        return world_x, world_y

    def screen_to_world_f(self, screen_x: float, screen_y: float) -> Tuple[float, float]:
        """Convert screen pixel coordinates to world coordinates (float, for zoom pivot)."""
        scale = self.zoom * self.cell_size
        world_x = screen_x / scale + self.x
        world_y = screen_y / scale + self.y
        return world_x, world_y


class Visualizer:
    """Main visualization class."""

    def __init__(self, width: int = 1280, height: int = 720, title: str = "PlantBrainGrid"):
        if not RAYLIB_AVAILABLE:
            raise ImportError("raylib/pyray is required for visualization. Install with: pip install raylib")

        self.width = width
        self.height = height
        self.title = title
        self.camera = Camera()
        self.selected_plant_id: Optional[int] = None
        self.show_water = False
        self.show_nutrients = False
        self.show_fire = True
        self.show_memory = False
        self.paused = False
        self.step_one = False   # True for exactly one frame when N is pressed
        self._initialized = False
        self._mono_font = None
        self._traced_plant_id: Optional[int] = None
        self._trace_history: list = []   # list of (ip: int, name: str)
        self._last_collected_tick: int = -1

    def initialize(self, fullscreen: bool = False):
        """Initialize raylib window."""
        if self._initialized:
            return
        if fullscreen:
            rl.set_config_flags(rl.FLAG_FULLSCREEN_MODE)
        rl.init_window(self.width, self.height, self.title)
        rl.set_target_fps(60)
        self._initialized = True
        for path in _MONO_FONT_CANDIDATES:
            if os.path.exists(path):
                self._mono_font = rl.load_font_ex(path, 12, None, 0)
                break

    def close(self):
        """Close raylib window."""
        if self._initialized:
            if self._mono_font is not None:
                rl.unload_font(self._mono_font)
                self._mono_font = None
            rl.close_window()
            self._initialized = False

    def should_close(self) -> bool:
        """Check if window should close."""
        return rl.window_should_close()

    def _apply_zoom(self, factor: float, pivot_screen_x: float, pivot_screen_y: float):
        """Zoom by factor, keeping the world point under pivot_screen fixed."""
        wx, wy = self.camera.screen_to_world_f(pivot_screen_x, pivot_screen_y)
        self.camera.zoom = max(0.1, min(20.0, self.camera.zoom * factor))
        scale = self.camera.zoom * self.camera.cell_size
        self.camera.x = wx - pivot_screen_x / scale
        self.camera.y = wy - pivot_screen_y / scale

    def handle_input(self):
        """Handle keyboard and mouse input."""
        # Pan with arrow keys or WASD (in world-cell units)
        pan_speed = 5.0 / self.camera.zoom
        if rl.is_key_down(rl.KEY_LEFT) or rl.is_key_down(rl.KEY_A):
            self.camera.x -= pan_speed
        if rl.is_key_down(rl.KEY_RIGHT) or rl.is_key_down(rl.KEY_D):
            self.camera.x += pan_speed
        if rl.is_key_down(rl.KEY_UP) or rl.is_key_down(rl.KEY_W):
            self.camera.y -= pan_speed
        if rl.is_key_down(rl.KEY_DOWN) or rl.is_key_down(rl.KEY_S):
            self.camera.y += pan_speed

        # Mouse-wheel zoom: pivot around the cursor
        wheel = rl.get_mouse_wheel_move()
        if wheel != 0:
            factor = 1.1 if wheel > 0 else (1.0 / 1.1)
            mx = float(rl.get_mouse_x())
            my = float(rl.get_mouse_y())
            self._apply_zoom(factor, mx, my)

        # Keyboard zoom: pivot around screen centre
        center_x, center_y = self.width / 2.0, self.height / 2.0
        if rl.is_key_pressed(rl.KEY_EQUAL) or rl.is_key_pressed(rl.KEY_KP_ADD):
            self._apply_zoom(1.2, center_x, center_y)
        if rl.is_key_pressed(rl.KEY_MINUS) or rl.is_key_pressed(rl.KEY_KP_SUBTRACT):
            self._apply_zoom(1.0 / 1.2, center_x, center_y)

        # Toggle overlays
        if rl.is_key_pressed(rl.KEY_ONE):
            self.show_water = not self.show_water
        if rl.is_key_pressed(rl.KEY_TWO):
            self.show_nutrients = not self.show_nutrients
        if rl.is_key_pressed(rl.KEY_THREE):
            self.show_fire = not self.show_fire
        if rl.is_key_pressed(rl.KEY_M):
            self.show_memory = not self.show_memory

        # Pause / step
        if rl.is_key_pressed(rl.KEY_SPACE):
            self.paused = not self.paused
        self.step_one = rl.is_key_pressed(rl.KEY_N)

        # Fullscreen toggle
        if rl.is_key_pressed(rl.KEY_F):
            rl.toggle_fullscreen()

        # Deselect
        if rl.is_key_pressed(rl.KEY_ESCAPE):
            self.selected_plant_id = None

    def _collect_trace(self, world, plants):
        """Manage brain tracing for the selected plant."""
        from plantbraingrid.brain_viewer import (OPCODES, NUM_OPCODES,
                                                  decode_instruction,
                                                  format_instruction)

        selected = next((p for p in plants if p.id() == self.selected_plant_id), None)

        if selected is None:
            # Deselected — disable tracing and clear history
            if self._traced_plant_id is not None:
                old = next((p for p in plants if p.id() == self._traced_plant_id), None)
                if old is not None:
                    old.brain().enable_tracing(False)
                self._traced_plant_id = None
                self._trace_history.clear()
                self._last_collected_tick = -1
            return

        brain = selected.brain()

        if self._traced_plant_id != self.selected_plant_id:
            # Selection changed — disable old plant's tracing, reset history
            if self._traced_plant_id is not None:
                old = next((p for p in plants if p.id() == self._traced_plant_id), None)
                if old is not None:
                    old.brain().enable_tracing(False)
            self._trace_history.clear()
            self._last_collected_tick = -1
            self._traced_plant_id = self.selected_plant_id
            brain.enable_tracing(True)
            return

        # Same plant — ensure tracing stays on and collect new steps
        brain.enable_tracing(True)
        current_tick = world.tick()
        if current_tick == self._last_collected_tick:
            return  # Paused: same tick, nothing new to collect
        self._last_collected_tick = current_tick

        trace = brain.last_trace()
        if trace is not None:
            mem = bytes(brain.memory())
            for step in trace['steps']:
                ip = step['ip']
                result = decode_instruction(mem, ip)
                if result is not None:
                    name, args, _ = result
                    formatted = format_instruction(name, args)
                else:
                    opcode = step['opcode'] % NUM_OPCODES
                    formatted = OPCODES.get(opcode, (f"UNK({opcode:02X})",))[0]
                self._trace_history.append((ip, formatted))
            if len(self._trace_history) > 200:
                self._trace_history = self._trace_history[-200:]

    def render_world(self, world, plants):
        """Render the world grid and plants."""
        # Sync every frame so fullscreen toggle and resize are always reflected
        self.width = rl.get_render_width()
        self.height = rl.get_render_height()

        self._collect_trace(world, plants)

        rl.begin_drawing()
        rl.clear_background(rl.Color(30, 30, 30, 255))

        # Cell size in screen pixels at the current zoom level
        cs = self.camera.cell_size
        scale = self.camera.zoom * cs
        cell_px = max(1, int(scale))

        # Visible world-cell range
        start_x = max(0, int(self.camera.x))
        start_y = max(0, int(self.camera.y))
        end_x = min(world.width(),  int(self.camera.x + self.width  / scale) + 2)
        end_y = min(world.height(), int(self.camera.y + self.height / scale) + 2)

        # Draw terrain overlays
        for y in range(start_y, end_y):
            for x in range(start_x, end_x):
                sx, sy = self.camera.world_to_screen(x, y)
                cell = world.cell_at(x, y)

                if self.show_water and cell.water_level > 0:
                    alpha = min(200, int(cell.water_level * 2))
                    rl.draw_rectangle(int(sx), int(sy), cell_px, cell_px,
                                      rl.Color(0, 100, 200, alpha))

                if self.show_nutrients and cell.nutrient_level > 0:
                    alpha = min(200, int(cell.nutrient_level * 3))
                    rl.draw_rectangle(int(sx), int(sy), cell_px, cell_px,
                                      rl.Color(139, 69, 19, alpha))

        # Draw plant cells
        for plant in plants:
            if not plant.is_alive():
                continue

            is_selected = self.selected_plant_id == plant.id()

            for cell in plant.cells():
                sx, sy = self.camera.world_to_screen(cell.position.x, cell.position.y)

                color = CELL_COLORS.get(int(cell.type), (255, 0, 255))
                if not cell.enabled:
                    color = tuple(c // 2 for c in color)

                rl.draw_rectangle(int(sx), int(sy), cell_px, cell_px,
                                  rl.Color(color[0], color[1], color[2], 255))

                if is_selected:
                    rl.draw_rectangle_lines(int(sx), int(sy), cell_px, cell_px,
                                            rl.Color(255, 255, 0, 255))

        # Draw fire overlay on top of plant cells
        if self.show_fire:
            for y in range(start_y, end_y):
                for x in range(start_x, end_x):
                    cell = world.cell_at(x, y)
                    if cell.is_on_fire():
                        sx, sy = self.camera.world_to_screen(x, y)
                        rl.draw_rectangle(int(sx), int(sy), cell_px, cell_px,
                                          rl.Color(255, 100, 0, 200))

        # Draw UI
        self._draw_ui(world, plants)

        rl.end_drawing()

    def _draw_mono(self, text: str, x: int, y: int, size: int, color):
        """Draw text using the loaded monospaced font, or fall back to the default font."""
        if self._mono_font is not None:
            rl.draw_text_ex(self._mono_font, text,
                            rl.Vector2(float(x), float(y)),
                            float(size), 0.0, color)
        else:
            rl.draw_text(text, x, y, size, color)

    def _draw_ui(self, world, plants):
        """Draw UI overlay."""
        status = f"Tick: {world.tick()}  Plants: {len(plants)}  "
        status += f"Zoom: {self.camera.zoom:.1f}x  "
        if self.paused:
            status += "[PAUSED]  "
        status += "1:Water 2:Nutrients 3:Fire  M:Memory  Space:Pause  N:Step  F:Fullscreen"

        rl.draw_rectangle(0, 0, self.width, 25, rl.Color(0, 0, 0, 180))
        rl.draw_text(status, 10, 5, 16, rl.Color(255, 255, 255, 255))

        if self.selected_plant_id is not None:
            for plant in plants:
                if plant.id() == self.selected_plant_id:
                    self._draw_plant_info(plant)
                    if self.show_memory:
                        mem_bottom = self._draw_memory_panel(plant)
                        self._draw_trace_panel(mem_bottom + 6)
                    break

    def _draw_plant_info(self, plant):
        """Draw info panel for selected plant."""
        try:
            from _plantbraingrid import CellType
        except ImportError:
            return

        # Count each cell type
        type_counts = {}
        for cell in plant.cells():
            t = cell.type
            type_counts[t] = type_counts.get(t, 0) + 1

        cell_type_names = [
            (CellType.Primary,       "Primary",        (180, 100, 40)),
            (CellType.SmallLeaf,     "SmallLeaf",      (34, 139, 34)),
            (CellType.BigLeaf,       "BigLeaf",        (0, 100, 0)),
            (CellType.FiberRoot,     "FiberRoot",      (139, 90, 43)),
            (CellType.TapRoot,       "TapRoot",        (110, 55, 90)),
            (CellType.Xylem,         "Xylem",          (210, 180, 140)),
            (CellType.FireproofXylem,"FproofXylem",    (255, 215, 0)),
            (CellType.Thorn,         "Thorn",          (128, 128, 128)),
            (CellType.FireStarter,   "FireStarter",    (255, 69, 0)),
        ]
        present = [(name, color, type_counts[ct]) for ct, name, color in cell_type_names if ct in type_counts]

        panel_width = 220
        # Header (id, age, cells, resources) + one row per present cell type
        panel_height = 5 + 20 + 18 * 4 + 8 + 16 * len(present) + 8
        panel_x = self.width - panel_width - 10
        panel_y = 35

        rl.draw_rectangle(panel_x, panel_y, panel_width, panel_height,
                         rl.Color(0, 0, 0, 200))
        rl.draw_rectangle_lines(panel_x, panel_y, panel_width, panel_height,
                               rl.Color(255, 255, 0, 255))

        y = panel_y + 5
        rl.draw_text(f"Plant #{plant.id()}", panel_x + 5, y, 16, rl.Color(255, 255, 255, 255))
        y += 20
        rl.draw_text(f"Age: {plant.age()}  Cells: {plant.cell_count()}", panel_x + 5, y, 14, rl.Color(200, 200, 200, 255))
        y += 18
        res = plant.resources()
        rl.draw_text(f"Energy:    {res.energy:.1f}", panel_x + 5, y, 14, rl.Color(255, 255, 100, 255))
        y += 18
        rl.draw_text(f"Water:     {res.water:.1f}", panel_x + 5, y, 14, rl.Color(100, 150, 255, 255))
        y += 18
        rl.draw_text(f"Nutrients: {res.nutrients:.1f}", panel_x + 5, y, 14, rl.Color(139, 100, 80, 255))
        y += 22

        rl.draw_text("Cell types:", panel_x + 5, y, 13, rl.Color(180, 180, 180, 255))
        y += 16
        for name, color, count in present:
            rl.draw_text(f"  {name}: {count}", panel_x + 5, y, 13,
                        rl.Color(color[0], color[1], color[2], 255))
            y += 16

    def _draw_memory_panel(self, plant):
        """Draw a hex-dump panel of the selected plant's brain memory."""
        brain = plant.brain()
        mem = bytes(brain.memory())
        ip = brain.ip()
        halted = brain.is_halted()

        font_size = 14
        row_height = 14
        bytes_per_row = 16
        visible_rows = 32
        panel_width = 420   # "XXXX: XX XX ... [XX] ... XX" × 16 at font_size=12
        panel_x = 10
        panel_y = 35
        header_height = 18

        total_rows = (len(mem) + bytes_per_row - 1) // bytes_per_row
        ip_row = ip // bytes_per_row

        # Keep the IP row within the visible window (one-third from top)
        start_row = max(0, ip_row - visible_rows // 3)
        start_row = min(start_row, max(0, total_rows - visible_rows))
        end_row = min(total_rows, start_row + visible_rows)

        panel_height = header_height + 4 + row_height * (end_row - start_row) + 4

        rl.draw_rectangle(panel_x, panel_y, panel_width, panel_height,
                          rl.Color(0, 0, 0, 210))
        rl.draw_rectangle_lines(panel_x, panel_y, panel_width, panel_height,
                                rl.Color(80, 180, 80, 255))

        # Header
        ip_label = "HALTED" if halted else f"IP=0x{ip:04X}"
        header = f"Brain memory  {ip_label}  ({len(mem)} bytes)"
        self._draw_mono(header, panel_x + 5, panel_y + 4, font_size,
                        rl.Color(100, 255, 100, 255))

        y = panel_y + header_height + 2
        for row in range(start_row, end_row):
            offset = row * bytes_per_row
            is_ip_row = (row == ip_row)

            if is_ip_row:
                rl.draw_rectangle(panel_x + 1, y, panel_width - 2, row_height,
                                  rl.Color(60, 60, 0, 255))

            # Build hex string for this row
            parts = [f"{offset:04X}: "]
            for col in range(bytes_per_row):
                if col == 8:
                    parts.append(" ")
                idx = offset + col
                if idx < len(mem):
                    byte = mem[idx]
                    if idx == ip:
                        parts.append(f"[{byte:02X}]")
                    else:
                        parts.append(f"{byte:02X} ")
                else:
                    parts.append("   ")

            line = "".join(parts)
            color = (rl.Color(255, 255, 80, 255) if is_ip_row
                     else rl.Color(160, 220, 160, 255))
            self._draw_mono(line, panel_x + 5, y, font_size, color)
            y += row_height

        # Scroll indicator if there are rows outside the visible window
        if total_rows > visible_rows:
            shown_pct = f"rows {start_row*16:#06x}–{(end_row-1)*16+15:#06x}"
            self._draw_mono(shown_pct, panel_x + 5, y + 1, font_size,
                            rl.Color(100, 140, 100, 255))

        return panel_y + panel_height

    def _draw_trace_panel(self, top_y: int):
        """Draw execution trace below the memory panel (newest instruction at bottom)."""
        if not self._trace_history:
            return

        font_size = 12
        row_height = 14
        panel_width = 420   # same width as memory panel
        panel_x = 10
        panel_y = top_y
        header_height = 18

        # How many rows fit between top_y and the bottom of the screen
        available_px = self.height - panel_y - 4
        visible_rows = max(1, (available_px - header_height - 8) // row_height)
        visible_rows = min(visible_rows, 20)

        history = self._trace_history[-visible_rows:]
        n_rows = len(history)
        panel_height = header_height + 4 + row_height * n_rows + 4

        rl.draw_rectangle(panel_x, panel_y, panel_width, panel_height,
                          rl.Color(0, 0, 0, 210))
        rl.draw_rectangle_lines(panel_x, panel_y, panel_width, panel_height,
                                rl.Color(80, 180, 80, 255))

        total = len(self._trace_history)
        header = f"Trace  ({total} steps)"
        self._draw_mono(header, panel_x + 5, panel_y + 4, font_size,
                        rl.Color(100, 255, 100, 255))

        y = panel_y + header_height + 2
        for i, (ip, formatted) in enumerate(history):
            is_last = (i == n_rows - 1)
            if is_last:
                rl.draw_rectangle(panel_x + 1, y, panel_width - 2, row_height,
                                  rl.Color(60, 60, 0, 255))
            line = f"{ip:04X}  {formatted}"
            color = (rl.Color(255, 255, 80, 255) if is_last
                     else rl.Color(160, 220, 160, 255))
            self._draw_mono(line, panel_x + 5, y, font_size, color)
            y += row_height

        if total > visible_rows:
            note = f"(+{total - visible_rows} earlier)"
            self._draw_mono(note, panel_x + 5, y + 1, font_size,
                            rl.Color(100, 140, 100, 255))

    def select_plant_at(self, screen_x: int, screen_y: int, plants) -> Optional[int]:
        """Select plant at screen position."""
        world_x, world_y = self.camera.screen_to_world(screen_x, screen_y)

        for plant in plants:
            if not plant.is_alive():
                continue
            for cell in plant.cells():
                if cell.position.x == world_x and cell.position.y == world_y:
                    self.selected_plant_id = plant.id()
                    return plant.id()

        self.selected_plant_id = None
        return None
