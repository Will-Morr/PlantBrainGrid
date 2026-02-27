"""Visualization module using raylib."""

from dataclasses import dataclass
from typing import Optional, Tuple
import math

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
    4: (139, 90, 43),     # Root (sienna)
    5: (210, 180, 140),   # Xylem (tan)
    6: (255, 215, 0),     # FireproofXylem (gold)
    7: (128, 128, 128),   # Thorn (gray)
    8: (255, 69, 0),      # FireStarter (red-orange)
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
        self.paused = False
        self._initialized = False

    def initialize(self):
        """Initialize raylib window."""
        if self._initialized:
            return
        rl.init_window(self.width, self.height, self.title)
        rl.set_target_fps(60)
        self._initialized = True

    def close(self):
        """Close raylib window."""
        if self._initialized:
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

        # Pause
        if rl.is_key_pressed(rl.KEY_SPACE):
            self.paused = not self.paused

        # Deselect
        if rl.is_key_pressed(rl.KEY_ESCAPE):
            self.selected_plant_id = None

    def render_world(self, world, plants):
        """Render the world grid and plants."""
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

                if self.show_fire and cell.is_on_fire():
                    rl.draw_rectangle(int(sx), int(sy), cell_px, cell_px,
                                      rl.Color(255, 100, 0, 200))

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

        # Draw UI
        self._draw_ui(world, plants)

        rl.end_drawing()

    def _draw_ui(self, world, plants):
        """Draw UI overlay."""
        status = f"Tick: {world.tick()}  Plants: {len(plants)}  "
        status += f"Zoom: {self.camera.zoom:.1f}x  "
        if self.paused:
            status += "[PAUSED]  "
        status += "1:Water 2:Nutrients 3:Fire  Space:Pause"

        rl.draw_rectangle(0, 0, self.width, 25, rl.Color(0, 0, 0, 180))
        rl.draw_text(status, 10, 5, 16, rl.Color(255, 255, 255, 255))

        if self.selected_plant_id is not None:
            for plant in plants:
                if plant.id() == self.selected_plant_id:
                    self._draw_plant_info(plant)
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
            (CellType.Root,          "Root",           (139, 90, 43)),
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
