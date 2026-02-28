#include "core/brain.hpp"
#include "core/brain_ops.hpp"
#include "core/plant.hpp"
#include "core/world.hpp"
#include "core/config.hpp"
#include <algorithm>
#include <cmath>

namespace pbg {

Brain::Brain(const std::vector<uint8_t>& genome)
    : memory_(genome)
{
    const auto& cfg = get_config();
    if (memory_.size() < cfg.brain_size) {
        memory_.resize(cfg.brain_size, 0);
    }
}

Brain::Brain(size_t size)
    : memory_(size, 0)
{
}

uint8_t Brain::read(uint16_t addr) const {
    if (addr >= memory_.size()) {
        return 0;  // OOB reads return 0
    }
    return memory_[addr];
}

void Brain::write(uint16_t addr, uint8_t value) {
    if (addr < memory_.size()) {
        memory_[addr] = value;
    }
    // OOB writes are silently ignored
}

void Brain::randomize_range(uint16_t start, uint16_t length, std::mt19937_64& rng) {
    std::uniform_int_distribution<uint16_t> dist(0, 255);
    for (uint16_t i = 0; i < length && (start + i) < memory_.size(); ++i) {
        memory_[start + i] = static_cast<uint8_t>(dist(rng));
    }
}

void Brain::push_stack(uint16_t addr) {
    call_stack_.push_back(addr);
}

uint16_t Brain::pop_stack() {
    if (call_stack_.empty()) {
        return 0;
    }
    uint16_t addr = call_stack_.back();
    call_stack_.pop_back();
    return addr;
}

uint8_t Brain::read_arg() {
    uint8_t val = read(ip_);
    ++ip_;
    if (ip_ >= memory_.size()) {
        ip_ = 0;  // Wrap around
    }
    return val;
}

uint16_t Brain::read_arg16() {
    uint8_t low = read_arg();
    uint8_t high = read_arg();
    return static_cast<uint16_t>(low) | (static_cast<uint16_t>(high) << 8);
}

int8_t Brain::read_arg_signed() {
    return static_cast<int8_t>(read_arg());
}

std::vector<QueuedAction> Brain::execute_tick(Plant& plant, World& world) {
    const auto& cfg = get_config();
    std::vector<QueuedAction> actions;

    ip_ = 0;
    call_stack_.clear();
    halted_ = false;
    oob_count_ = 0;

    if (trace_enabled_) {
        last_trace_ = ExecutionTrace{};
    }

    uint32_t instruction_count = 0;

    while (!halted_ && instruction_count < cfg.max_instructions_per_tick) {
        if (!execute_instruction(plant, world, actions)) {
            break;
        }
        ++instruction_count;
    }

    // Apply penalties
    if (instruction_count >= cfg.max_instructions_per_tick) {
        plant.resources().energy -= cfg.instruction_limit_penalty;
        plant.resources().clamp_non_negative();
        if (trace_enabled_ && last_trace_) {
            last_trace_->hit_instruction_limit = true;
        }
    }

    if (oob_count_ > 0) {
        plant.resources().energy -= cfg.oob_memory_penalty * oob_count_;
        plant.resources().clamp_non_negative();
        if (trace_enabled_ && last_trace_) {
            last_trace_->hit_oob_memory = true;
            last_trace_->oob_count = oob_count_;
        }
    }

    return actions;
}

bool Brain::execute_instruction(Plant& plant, World& world, std::vector<QueuedAction>& actions) {
    const auto& cfg = get_config();

    // Fetch and decode
    uint16_t start_ip = ip_;
    uint8_t raw_opcode = read_arg();
    uint8_t opcode = raw_opcode % NUM_OPCODES;

    if (trace_enabled_ && last_trace_) {
        ExecutionStep step;
        step.ip = start_ip;
        step.opcode = opcode;
        last_trace_->steps.push_back(step);
    }

    // Helper to check memory bounds and track OOB
    auto safe_read = [this](uint16_t addr) -> uint8_t {
        if (addr >= memory_.size()) {
            ++oob_count_;
            return 0;
        }
        return memory_[addr];
    };

    auto safe_write = [this](uint16_t addr, uint8_t val) {
        if (addr >= memory_.size()) {
            ++oob_count_;
            return;
        }
        memory_[addr] = val;
    };

    switch (opcode) {
        // Control Flow
        case OP_NOP:
            break;

        case OP_HALT:
            halted_ = true;
            ip_ += 1; // Increment instuction pointer to not just stop forever
            return false;

        case OP_JUMP: {
            uint16_t addr = read_arg16();
            ip_ = addr % memory_.size();
            break;
        }

        case OP_JUMP_REL: {
            int8_t offset = read_arg_signed();
            int32_t new_ip = static_cast<int32_t>(ip_) + offset;
            while (new_ip < 0) new_ip += memory_.size();
            ip_ = static_cast<uint16_t>(new_ip % memory_.size());
            break;
        }

        case OP_JUMP_IF_ZERO: {
            uint16_t test_addr = read_arg16();
            uint16_t jump_addr = read_arg16();
            if (safe_read(test_addr) == 0) {
                ip_ = jump_addr % memory_.size();
            }
            break;
        }

        case OP_JUMP_IF_NEQ: {
            uint16_t test_addr = read_arg16();
            uint8_t compare_val = read_arg();
            uint16_t jump_addr = read_arg16();
            if (safe_read(test_addr) != compare_val) {
                ip_ = jump_addr % memory_.size();
            }
            break;
        }

        case OP_CALL: {
            uint16_t addr = read_arg16();
            push_stack(ip_);
            ip_ = addr % memory_.size();
            break;
        }

        case OP_RET: {
            ip_ = pop_stack();
            break;
        }

        // Memory Operations
        case OP_LOAD_IMM: {
            uint16_t addr = read_arg16();
            uint8_t val = read_arg();
            safe_write(addr, val);
            break;
        }

        case OP_COPY: {
            uint16_t dest = read_arg16();
            uint16_t src = read_arg16();
            safe_write(dest, safe_read(src));
            break;
        }

        case OP_ADD: {
            uint16_t dest = read_arg16();
            uint16_t a = read_arg16();
            uint16_t b = read_arg16();
            safe_write(dest, safe_read(a) + safe_read(b));
            break;
        }

        case OP_SUB: {
            uint16_t dest = read_arg16();
            uint16_t a = read_arg16();
            uint16_t b = read_arg16();
            safe_write(dest, safe_read(a) - safe_read(b));
            break;
        }

        case OP_MUL: {
            uint16_t dest = read_arg16();
            uint16_t a = read_arg16();
            uint16_t b = read_arg16();
            safe_write(dest, safe_read(a) * safe_read(b));
            break;
        }

        case OP_DIV: {
            uint16_t dest = read_arg16();
            uint16_t a = read_arg16();
            uint16_t b = read_arg16();
            uint8_t divisor = safe_read(b);
            safe_write(dest, divisor == 0 ? 0 : safe_read(a) / divisor);
            break;
        }

        case OP_MOD: {
            uint16_t dest = read_arg16();
            uint16_t a = read_arg16();
            uint16_t b = read_arg16();
            uint8_t divisor = safe_read(b);
            safe_write(dest, divisor == 0 ? 0 : safe_read(a) % divisor);
            break;
        }

        case OP_AND: {
            uint16_t dest = read_arg16();
            uint16_t a = read_arg16();
            uint16_t b = read_arg16();
            safe_write(dest, safe_read(a) & safe_read(b));
            break;
        }

        case OP_OR: {
            uint16_t dest = read_arg16();
            uint16_t a = read_arg16();
            uint16_t b = read_arg16();
            safe_write(dest, safe_read(a) | safe_read(b));
            break;
        }

        case OP_XOR: {
            uint16_t dest = read_arg16();
            uint16_t a = read_arg16();
            uint16_t b = read_arg16();
            safe_write(dest, safe_read(a) ^ safe_read(b));
            break;
        }

        case OP_NOT: {
            uint16_t dest = read_arg16();
            uint16_t src = read_arg16();
            safe_write(dest, ~safe_read(src));
            break;
        }

        case OP_SHL: {
            uint16_t dest = read_arg16();
            uint16_t src = read_arg16();
            uint8_t amount = read_arg();
            safe_write(dest, safe_read(src) << (amount & 7));
            break;
        }

        case OP_SHR: {
            uint16_t dest = read_arg16();
            uint16_t src = read_arg16();
            uint8_t amount = read_arg();
            safe_write(dest, safe_read(src) >> (amount & 7));
            break;
        }

        case OP_CMP_LT: {
            uint16_t dest = read_arg16();
            uint16_t a = read_arg16();
            uint16_t b = read_arg16();
            safe_write(dest, safe_read(a) < safe_read(b) ? 1 : 0);
            break;
        }

        case OP_CMP_EQ: {
            uint16_t dest = read_arg16();
            uint16_t a = read_arg16();
            uint16_t b = read_arg16();
            safe_write(dest, safe_read(a) == safe_read(b) ? 1 : 0);
            break;
        }

        case OP_LOAD_IND: {
            uint16_t dest = read_arg16();
            uint16_t addr_ptr = read_arg16();
            uint16_t indirect_addr = safe_read(addr_ptr) |
                                     (static_cast<uint16_t>(safe_read(addr_ptr + 1)) << 8);
            safe_write(dest, safe_read(indirect_addr));
            break;
        }

        case OP_STORE_IND: {
            uint16_t addr_ptr = read_arg16();
            uint16_t src = read_arg16();
            uint16_t indirect_addr = safe_read(addr_ptr) |
                                     (static_cast<uint16_t>(safe_read(addr_ptr + 1)) << 8);
            safe_write(indirect_addr, safe_read(src));
            break;
        }

        case OP_RANDOMIZE: {
            uint16_t start = read_arg16();
            uint8_t length = read_arg();
            randomize_range(start, length, world.rng());
            break;
        }

        // World Sensing
        case OP_SENSE_WATER: {
            uint16_t dest = read_arg16();
            int8_t dx = read_arg_signed();
            int8_t dy = read_arg_signed();
            GridCoord pos = plant.primary_position() + GridCoord{dx, dy};

            uint8_t value = 0;
            if (std::abs(dx) <= cfg.vision_radius && std::abs(dy) <= cfg.vision_radius) {
                if (world.in_bounds(pos)) {
                    value = static_cast<uint8_t>(std::min(255.0f,
                        world.cell_at(pos).water_level * cfg.resource_sense_scale));
                }
            }
            safe_write(dest, value);
            break;
        }

        case OP_SENSE_NUTRIENTS: {
            uint16_t dest = read_arg16();
            int8_t dx = read_arg_signed();
            int8_t dy = read_arg_signed();
            GridCoord pos = plant.primary_position() + GridCoord{dx, dy};

            uint8_t value = 0;
            if (std::abs(dx) <= cfg.vision_radius && std::abs(dy) <= cfg.vision_radius) {
                if (world.in_bounds(pos)) {
                    value = static_cast<uint8_t>(std::min(255.0f,
                        world.cell_at(pos).nutrient_level * cfg.resource_sense_scale));
                }
            }
            safe_write(dest, value);
            break;
        }

        case OP_SENSE_LIGHT: {
            uint16_t dest = read_arg16();
            uint8_t value = static_cast<uint8_t>(world.current_light_multiplier() * 255);
            safe_write(dest, value);
            break;
        }

        case OP_SENSE_CELL: {
            uint16_t dest = read_arg16();
            int8_t dx = read_arg_signed();
            int8_t dy = read_arg_signed();
            GridCoord pos = plant.primary_position() + GridCoord{dx, dy};

            uint8_t value = static_cast<uint8_t>(CellType::Empty);
            if (std::abs(dx) <= cfg.vision_radius && std::abs(dy) <= cfg.vision_radius) {
                if (world.in_bounds(pos)) {
                    const WorldCell& wc = world.cell_at(pos);
                    if (wc.is_occupied()) {
                        value = static_cast<uint8_t>(wc.cell_type);
                    }
                }
            }
            safe_write(dest, value);
            break;
        }

        case OP_SENSE_FIRE: {
            uint16_t dest = read_arg16();
            int8_t dx = read_arg_signed();
            int8_t dy = read_arg_signed();
            GridCoord pos = plant.primary_position() + GridCoord{dx, dy};

            uint8_t value = 0;
            if (std::abs(dx) <= cfg.vision_radius && std::abs(dy) <= cfg.vision_radius) {
                if (world.in_bounds(pos)) {
                    value = world.cell_at(pos).is_on_fire() ? 255 : 0;
                }
            }
            safe_write(dest, value);
            break;
        }

        case OP_SENSE_OWNED: {
            uint16_t dest = read_arg16();
            int8_t dx = read_arg_signed();
            int8_t dy = read_arg_signed();
            GridCoord pos = plant.primary_position() + GridCoord{dx, dy};

            uint8_t value = 0;
            if (std::abs(dx) <= cfg.vision_radius && std::abs(dy) <= cfg.vision_radius) {
                if (plant.find_cell(pos) != nullptr) {
                    value = 1;
                }
            }
            safe_write(dest, value);
            break;
        }

        case OP_SENSE_SELF_ENERGY: {
            uint16_t dest = read_arg16();
            uint8_t value = static_cast<uint8_t>(std::min(255.0f,
                plant.resources().energy * cfg.resource_sense_scale));
            safe_write(dest, value);
            break;
        }

        case OP_SENSE_SELF_WATER: {
            uint16_t dest = read_arg16();
            uint8_t value = static_cast<uint8_t>(std::min(255.0f,
                plant.resources().water * cfg.resource_sense_scale));
            safe_write(dest, value);
            break;
        }

        case OP_SENSE_SELF_NUTRIENTS: {
            uint16_t dest = read_arg16();
            uint8_t value = static_cast<uint8_t>(std::min(255.0f,
                plant.resources().nutrients * cfg.resource_sense_scale));
            safe_write(dest, value);
            break;
        }

        case OP_SENSE_CELL_COUNT: {
            uint16_t dest = read_arg16();
            uint8_t value = static_cast<uint8_t>(std::min(255UL, plant.cell_count()));
            safe_write(dest, value);
            break;
        }

        case OP_SENSE_AGE: {
            uint16_t dest = read_arg16();
            uint8_t value = static_cast<uint8_t>(std::min(255UL, plant.age() / 100));
            safe_write(dest, value);
            break;
        }

        // Plant Actions
        case OP_PLACE_CELL: {
            uint8_t type_byte = read_arg();
            int8_t dx = read_arg_signed();
            int8_t dy = read_arg_signed();
            uint8_t dir_byte = read_arg();

            CellType type = static_cast<CellType>(type_byte % 9);
            Direction dir = direction_from_byte(dir_byte);
            GridCoord pos = plant.primary_position() + GridCoord{dx, dy};

            QueuedAction action;
            action.type = ActionType::PlaceCell;
            action.position = pos;
            action.cell_type = type;
            action.direction = dir;
            actions.push_back(action);
            break;
        }

        case OP_ROTATE_CELL: {
            int8_t dx = read_arg_signed();
            int8_t dy = read_arg_signed();
            int8_t rotation = read_arg_signed();

            GridCoord pos = plant.primary_position() + GridCoord{dx, dy};

            QueuedAction action;
            action.type = ActionType::RotateCell;
            action.position = pos;
            action.rotation = rotation;
            actions.push_back(action);
            break;
        }

        case OP_TOGGLE_CELL: {
            int8_t dx = read_arg_signed();
            int8_t dy = read_arg_signed();
            uint8_t enabled = read_arg();

            GridCoord pos = plant.primary_position() + GridCoord{dx, dy};

            QueuedAction action;
            action.type = ActionType::ToggleCell;
            action.position = pos;
            action.toggle_state = enabled != 0;
            actions.push_back(action);
            break;
        }

        case OP_REMOVE_CELL: {
            int8_t dx = read_arg_signed();
            int8_t dy = read_arg_signed();

            GridCoord pos = plant.primary_position() + GridCoord{dx, dy};

            QueuedAction action;
            action.type = ActionType::RemoveCell;
            action.position = pos;
            actions.push_back(action);
            break;
        }

        // Reproduction
        case OP_START_MATE_SEARCH: {
            uint8_t max_dist = read_arg();
            mate_search_.active = true;
            mate_search_.max_distance = static_cast<float>(max_dist);
            mate_search_.weights.clear();
            mate_search_.selected_mate_id = 0;
            break;
        }

        case OP_ADD_MATE_WEIGHT: {
            uint8_t criterion = read_arg();
            uint8_t weight = read_arg();
            if (mate_search_.active) {
                mate_search_.weights.emplace_back(criterion, weight);
            }
            break;
        }

        case OP_FINISH_MATE_SELECT: {
            // Mate selection will be handled by simulation loop
            // which has access to all plants
            mate_search_.active = false;
            break;
        }

        case OP_LAUNCH_SEED: {
            // Read fixed 8-byte parameter block
            uint8_t recomb = read_arg();
            uint8_t energy = read_arg();
            uint8_t water = read_arg();
            uint8_t nutrients = read_arg();
            uint8_t power = read_arg();
            int8_t dx = read_arg_signed();
            int8_t dy = read_arg_signed();
            uint8_t placement = read_arg();

            QueuedAction action;
            action.type = ActionType::LaunchSeed;
            action.seed_params = QueuedAction::SeedParams{
                static_cast<RecombinationMethod>(recomb % 7),
                energy,
                water,
                nutrients,
                power,
                dx,
                dy,
                static_cast<SeedPlacementMode>(placement & 1)
            };
            actions.push_back(action);
            break;
        }

        default:
            // Unknown opcode - treat as NOP
            break;
    }

    return true;
}

}  // namespace pbg
