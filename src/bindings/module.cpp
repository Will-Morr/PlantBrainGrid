#include <pybind11/pybind11.h>
#include <pybind11/stl.h>
#include <pybind11/functional.h>
#include "core/types.hpp"
#include "core/config.hpp"
#include "core/world.hpp"
#include "core/plant.hpp"
#include "core/brain.hpp"
#include "core/simulation.hpp"
#include "core/reproduction.hpp"
#include "core/resources.hpp"

namespace py = pybind11;
using namespace pbg;

PYBIND11_MODULE(_plantbraingrid, m) {
    m.doc() = "PlantBrainGrid simulation core";

    // GridCoord
    py::class_<GridCoord>(m, "GridCoord")
        .def(py::init<>())
        .def(py::init<int32_t, int32_t>())
        .def_readwrite("x", &GridCoord::x)
        .def_readwrite("y", &GridCoord::y)
        .def("__eq__", &GridCoord::operator==)
        .def("__repr__", [](const GridCoord& c) {
            return "GridCoord(" + std::to_string(c.x) + ", " + std::to_string(c.y) + ")";
        });

    // Direction
    py::enum_<Direction>(m, "Direction")
        .value("North", Direction::North)
        .value("East", Direction::East)
        .value("South", Direction::South)
        .value("West", Direction::West);

    // CellType
    py::enum_<CellType>(m, "CellType")
        .value("Empty", CellType::Empty)
        .value("Primary", CellType::Primary)
        .value("SmallLeaf", CellType::SmallLeaf)
        .value("BigLeaf", CellType::BigLeaf)
        .value("Root", CellType::Root)
        .value("Xylem", CellType::Xylem)
        .value("FireproofXylem", CellType::FireproofXylem)
        .value("Thorn", CellType::Thorn)
        .value("FireStarter", CellType::FireStarter);

    // RecombinationMethod
    py::enum_<RecombinationMethod>(m, "RecombinationMethod")
        .value("MotherOnly", RecombinationMethod::MotherOnly)
        .value("FatherOnly", RecombinationMethod::FatherOnly)
        .value("Mother75", RecombinationMethod::Mother75)
        .value("Father75", RecombinationMethod::Father75)
        .value("HalfHalf", RecombinationMethod::HalfHalf)
        .value("RandomMix", RecombinationMethod::RandomMix)
        .value("Alternating", RecombinationMethod::Alternating);

    // Config
    py::class_<Config>(m, "Config")
        .def(py::init<>())
        .def_readwrite("world_width", &Config::world_width)
        .def_readwrite("world_height", &Config::world_height)
        .def_readwrite("brain_size", &Config::brain_size)
        .def_readwrite("vision_radius", &Config::vision_radius)
        .def_readwrite("max_instructions_per_tick", &Config::max_instructions_per_tick)
        .def_readwrite("oob_memory_penalty", &Config::oob_memory_penalty)
        .def_readwrite("instruction_limit_penalty", &Config::instruction_limit_penalty)
        .def_readwrite("mutation_rate", &Config::mutation_rate)
        .def_readwrite("season_length", &Config::season_length)
        .def_readwrite("xylem_transfer_cost", &Config::xylem_transfer_cost)
        .def_readwrite("fire_spread_ticks", &Config::fire_spread_ticks)
        .def_readwrite("fire_destroy_ticks", &Config::fire_destroy_ticks);

    m.def("get_config", &get_config, py::return_value_policy::reference);

    // WorldCell
    py::class_<WorldCell>(m, "WorldCell")
        .def_readwrite("water_level", &WorldCell::water_level)
        .def_readwrite("nutrient_level", &WorldCell::nutrient_level)
        .def_readwrite("light_level", &WorldCell::light_level)
        .def_readonly("fire_ticks", &WorldCell::fire_ticks)
        .def_readonly("plant_id", &WorldCell::plant_id)
        .def_readonly("cell_type", &WorldCell::cell_type)
        .def("is_on_fire", &WorldCell::is_on_fire)
        .def("is_occupied", &WorldCell::is_occupied);

    // World
    py::class_<World>(m, "World")
        .def(py::init<uint32_t, uint32_t, uint64_t>())
        .def("width", &World::width)
        .def("height", &World::height)
        .def("seed", &World::seed)
        .def("tick", &World::tick)
        .def("in_bounds", py::overload_cast<int32_t, int32_t>(&World::in_bounds, py::const_))
        .def("in_bounds", py::overload_cast<const GridCoord&>(&World::in_bounds, py::const_))
        .def("cell_at", py::overload_cast<int32_t, int32_t>(&World::cell_at),
             py::return_value_policy::reference)
        .def("cell_at", py::overload_cast<const GridCoord&>(&World::cell_at),
             py::return_value_policy::reference)
        .def("current_light_multiplier", &World::current_light_multiplier)
        .def("ignite", &World::ignite)
        .def("advance_tick", &World::advance_tick)
        .def("regenerate_terrain", &World::regenerate_terrain);

    // Resources
    py::class_<Resources>(m, "Resources")
        .def(py::init<>())
        .def(py::init<float, float, float>())
        .def_readwrite("energy", &Resources::energy)
        .def_readwrite("water", &Resources::water)
        .def_readwrite("nutrients", &Resources::nutrients);

    // PlantCell
    py::class_<PlantCell>(m, "PlantCell")
        .def_readonly("type", &PlantCell::type)
        .def_readonly("position", &PlantCell::position)
        .def_readonly("enabled", &PlantCell::enabled)
        .def_readonly("direction", &PlantCell::direction)
        .def_readonly("plant_id", &PlantCell::plant_id)
        .def("is_xylem", &PlantCell::is_xylem)
        .def("is_leaf", &PlantCell::is_leaf);

    // Brain (partial exposure for inspection)
    py::class_<Brain>(m, "Brain")
        .def("read", &Brain::read)
        .def("write", &Brain::write)
        .def("size", &Brain::size)
        .def("ip", &Brain::ip)
        .def("is_halted", &Brain::is_halted)
        .def("enable_tracing", &Brain::enable_tracing)
        .def("last_trace", [](const Brain& b) -> py::object {
            const auto& trace = b.last_trace();
            if (!trace.has_value()) return py::none();
            // Return as dict for simplicity
            py::dict d;
            d["hit_instruction_limit"] = trace->hit_instruction_limit;
            d["hit_oob_memory"] = trace->hit_oob_memory;
            d["oob_count"] = trace->oob_count;
            py::list steps;
            for (const auto& step : trace->steps) {
                py::dict s;
                s["ip"] = step.ip;
                s["opcode"] = step.opcode;
                steps.append(s);
            }
            d["steps"] = steps;
            return d;
        })
        .def("memory", [](const Brain& b) {
            return py::bytes(reinterpret_cast<const char*>(b.memory().data()), b.memory().size());
        });

    // Plant
    py::class_<Plant>(m, "Plant")
        .def(py::init<uint64_t, const GridCoord&, const std::vector<uint8_t>&>())
        .def("id", &Plant::id)
        .def("primary_position", &Plant::primary_position)
        .def("is_alive", &Plant::is_alive)
        .def("age", &Plant::age)
        .def("cell_count", &Plant::cell_count)
        .def("resources", py::overload_cast<>(&Plant::resources),
             py::return_value_policy::reference)
        .def("brain", py::overload_cast<>(&Plant::brain),
             py::return_value_policy::reference)
        .def("cells", &Plant::cells, py::return_value_policy::reference)
        .def("can_place_cell", &Plant::can_place_cell)
        .def("place_cell", &Plant::place_cell)
        .def("remove_cell", &Plant::remove_cell)
        .def("toggle_cell", &Plant::toggle_cell)
        .def("rotate_cell", &Plant::rotate_cell)
        .def("kill", &Plant::kill);

    // Seed
    py::class_<Seed>(m, "Seed")
        .def(py::init<>())
        .def_readwrite("genome", &Seed::genome)
        .def_readwrite("energy", &Seed::energy)
        .def_readwrite("water", &Seed::water)
        .def_readwrite("nutrients", &Seed::nutrients)
        .def_readwrite("position", &Seed::position)
        .def_property("velocity",
            [](const Seed& s) { return std::make_pair(s.velocity.x, s.velocity.y); },
            [](Seed& s, std::pair<float,float> v) { s.velocity.x = v.first; s.velocity.y = v.second; }
        )
        .def_readwrite("in_flight", &Seed::in_flight)
        .def_readwrite("flight_ticks_remaining", &Seed::flight_ticks_remaining)
        .def_readwrite("mother_id", &Seed::mother_id)
        .def_readwrite("father_id", &Seed::father_id);

    // TickStats
    py::class_<TickStats>(m, "TickStats")
        .def(py::init<>())
        .def_readonly("tick", &TickStats::tick)
        .def_readonly("plant_count", &TickStats::plant_count)
        .def_readonly("seed_count", &TickStats::seed_count)
        .def_readonly("cells_placed", &TickStats::cells_placed)
        .def_readonly("cells_removed", &TickStats::cells_removed)
        .def_readonly("placements_cancelled", &TickStats::placements_cancelled)
        .def_readonly("seeds_launched", &TickStats::seeds_launched)
        .def_readonly("seeds_germinated", &TickStats::seeds_germinated)
        .def_readonly("plants_died", &TickStats::plants_died);

    // Simulation
    py::class_<Simulation>(m, "Simulation")
        .def(py::init<uint32_t, uint32_t, uint64_t>())
        .def("world", py::overload_cast<>(&Simulation::world),
             py::return_value_policy::reference)
        .def("plants", &Simulation::plants, py::return_value_policy::reference)
        .def("seeds", &Simulation::seeds, py::return_value_policy::reference)
        .def("tick", &Simulation::tick)
        .def("add_plant", &Simulation::add_plant, py::return_value_policy::reference)
        .def("find_plant", py::overload_cast<uint64_t>(&Simulation::find_plant),
             py::return_value_policy::reference)
        .def("remove_dead_plants", &Simulation::remove_dead_plants)
        .def("add_seed", &Simulation::add_seed)
        .def("update_seeds", &Simulation::update_seeds)
        .def("germinate_seeds", &Simulation::germinate_seeds)
        .def("advance_tick", &Simulation::advance_tick)
        .def("run", &Simulation::run)
        .def("save_state", &Simulation::save_state)
        .def("load_state", &Simulation::load_state)
        .def("enable_auto_spawn", &Simulation::enable_auto_spawn,
             py::arg("enable"),
             py::arg("min_population") = 10,
             py::arg("energy") = 100.0f,
             py::arg("water") = 50.0f,
             py::arg("nutrients") = 30.0f)
        .def_property_readonly("auto_spawn_enabled", &Simulation::auto_spawn_enabled)
        .def_property_readonly("auto_spawn_min_population", &Simulation::auto_spawn_min_population)
        .def("on_plant_death", [](Simulation& sim, py::function callback) {
            sim.on_plant_death([callback](const Plant& p) {
                callback(&p);
            });
        })
        .def("on_plant_birth", [](Simulation& sim, py::function callback) {
            sim.on_plant_birth([callback](const Plant& p) {
                callback(&p);
            });
        })
        .def("on_seed_launch", [](Simulation& sim, py::function callback) {
            sim.on_seed_launch([callback](const Seed& s) {
                callback(&s);
            });
        });

    // ResourceSystem (static methods)
    py::class_<ResourceSystem>(m, "ResourceSystem")
        .def_static("process_tick", &ResourceSystem::process_tick)
        .def_static("calculate_leaf_energy", &ResourceSystem::calculate_leaf_energy)
        .def_static("calculate_root_water", &ResourceSystem::calculate_root_water)
        .def_static("calculate_root_nutrients", &ResourceSystem::calculate_root_nutrients)
        .def_static("calculate_maintenance", &ResourceSystem::calculate_maintenance);

    // ResourceTickResult
    py::class_<ResourceTickResult>(m, "ResourceTickResult")
        .def(py::init<>())
        .def_readonly("energy_generated", &ResourceTickResult::energy_generated)
        .def_readonly("water_extracted", &ResourceTickResult::water_extracted)
        .def_readonly("nutrients_extracted", &ResourceTickResult::nutrients_extracted)
        .def_readonly("energy_maintenance", &ResourceTickResult::energy_maintenance)
        .def_readonly("water_maintenance", &ResourceTickResult::water_maintenance)
        .def_readonly("nutrients_maintenance", &ResourceTickResult::nutrients_maintenance)
        .def_readonly("net_energy", &ResourceTickResult::net_energy)
        .def_readonly("net_water", &ResourceTickResult::net_water)
        .def_readonly("net_nutrients", &ResourceTickResult::net_nutrients);
}
