#pragma once

#include "core/types.hpp"
#include <vector>
#include <cstdint>
#include <optional>
#include <random>

namespace pbg {

// Forward declarations
class Plant;
class World;

// Action types that brains can queue
enum class ActionType : uint8_t {
    PlaceCell,
    RemoveCell,
    ToggleCell,
    RotateCell,
    LaunchSeed
};

struct QueuedAction {
    ActionType type;
    GridCoord position;
    CellType cell_type = CellType::Empty;
    Direction direction = Direction::North;
    bool toggle_state = true;
    int rotation = 0;

    // For seed launching
    struct SeedParams {
        RecombinationMethod recomb_method;
        uint8_t energy;
        uint8_t water;
        uint8_t nutrients;
        uint8_t launch_power;
        int8_t dx;
        int8_t dy;
        SeedPlacementMode placement_mode;
    };
    std::optional<SeedParams> seed_params;
};

// Execution trace for visualization
struct ExecutionStep {
    uint16_t ip;
    uint8_t opcode;
    std::vector<uint8_t> args;
};

struct ExecutionTrace {
    std::vector<ExecutionStep> steps;
    bool hit_instruction_limit = false;
    bool hit_oob_memory = false;
    uint32_t oob_count = 0;
};

// Mate selection state
struct MateSearchState {
    bool active = false;
    float max_distance = 0.0f;
    std::vector<std::pair<uint8_t, uint8_t>> weights;  // (criterion, weight)
    uint64_t selected_mate_id = 0;
};

class Brain {
public:
    explicit Brain(const std::vector<uint8_t>& genome);
    Brain(size_t size = 1024);

    // Memory access
    uint8_t read(uint16_t addr) const;
    void write(uint16_t addr, uint8_t value);
    void randomize_range(uint16_t start, uint16_t length, std::mt19937_64& rng);

    // Memory inspection
    const std::vector<uint8_t>& memory() const { return memory_; }
    std::vector<uint8_t>& memory() { return memory_; }
    size_t size() const { return memory_.size(); }

    // Instruction pointer
    uint16_t ip() const { return ip_; }
    void set_ip(uint16_t ip) { ip_ = ip; }

    // Execution state
    bool is_halted() const { return halted_; }
    void halt() { halted_ = true; }
    void reset_halt() { halted_ = false; }

    // Execute one tick of brain logic
    // Returns queued actions and modifies plant resources on errors
    std::vector<QueuedAction> execute_tick(Plant& plant, World& world);

    // Execution trace for debugging/visualization
    void enable_tracing(bool enable) { trace_enabled_ = enable; }
    const std::optional<ExecutionTrace>& last_trace() const { return last_trace_; }

    // Mate selection
    MateSearchState& mate_search() { return mate_search_; }
    const MateSearchState& mate_search() const { return mate_search_; }

    // Call stack (for CALL/RET)
    void push_stack(uint16_t addr);
    uint16_t pop_stack();
    bool stack_empty() const { return call_stack_.empty(); }

private:
    std::vector<uint8_t> memory_;
    uint16_t ip_ = 0;
    bool halted_ = false;
    bool trace_enabled_ = false;
    std::optional<ExecutionTrace> last_trace_;
    std::vector<uint16_t> call_stack_;
    MateSearchState mate_search_;

    // Out-of-bounds tracking for current tick
    uint32_t oob_count_ = 0;

    // Execute a single instruction, returns true if should continue
    bool execute_instruction(Plant& plant, World& world, std::vector<QueuedAction>& actions);

    // Read argument bytes from memory (advances ip)
    uint8_t read_arg();
    uint16_t read_arg16();
    int8_t read_arg_signed();
};

}  // namespace pbg
