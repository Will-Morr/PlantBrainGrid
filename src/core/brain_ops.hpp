#pragma once

#include <cstdint>

namespace pbg {

// Opcode definitions
// Total number of opcodes determines modulo for instruction decode
constexpr uint8_t NUM_OPCODES = 0xA0;

// Control Flow (0x00-0x1F)
constexpr uint8_t OP_NOP = 0x00;
constexpr uint8_t OP_HALT = 0x01;
constexpr uint8_t OP_JUMP = 0x02;
constexpr uint8_t OP_JUMP_REL = 0x03;
constexpr uint8_t OP_JUMP_IF_ZERO = 0x04;
constexpr uint8_t OP_JUMP_IF_NEQ = 0x05;
constexpr uint8_t OP_CALL = 0x06;
constexpr uint8_t OP_RET = 0x07;

// Memory Operations (0x20-0x3F)
constexpr uint8_t OP_LOAD_IMM = 0x20;
constexpr uint8_t OP_COPY = 0x21;
constexpr uint8_t OP_ADD = 0x22;
constexpr uint8_t OP_SUB = 0x23;
constexpr uint8_t OP_MUL = 0x24;
constexpr uint8_t OP_DIV = 0x25;
constexpr uint8_t OP_MOD = 0x26;
constexpr uint8_t OP_AND = 0x27;
constexpr uint8_t OP_OR = 0x28;
constexpr uint8_t OP_XOR = 0x29;
constexpr uint8_t OP_NOT = 0x2A;
constexpr uint8_t OP_SHL = 0x2B;
constexpr uint8_t OP_SHR = 0x2C;
constexpr uint8_t OP_CMP_LT = 0x2D;
constexpr uint8_t OP_CMP_EQ = 0x2E;
constexpr uint8_t OP_LOAD_IND = 0x2F;
constexpr uint8_t OP_STORE_IND = 0x30;
constexpr uint8_t OP_RANDOMIZE = 0x31;

// World Sensing (0x40-0x5F)
constexpr uint8_t OP_SENSE_WATER = 0x40;
constexpr uint8_t OP_SENSE_NUTRIENTS = 0x41;
constexpr uint8_t OP_SENSE_LIGHT = 0x42;
constexpr uint8_t OP_SENSE_CELL = 0x43;
constexpr uint8_t OP_SENSE_FIRE = 0x44;
constexpr uint8_t OP_SENSE_OWNED = 0x45;
constexpr uint8_t OP_SENSE_SELF_ENERGY = 0x46;
constexpr uint8_t OP_SENSE_SELF_WATER = 0x47;
constexpr uint8_t OP_SENSE_SELF_NUTRIENTS = 0x48;
constexpr uint8_t OP_SENSE_CELL_COUNT = 0x49;
constexpr uint8_t OP_SENSE_AGE = 0x4A;

// Plant Actions (0x60-0x7F)
constexpr uint8_t OP_PLACE_CELL = 0x60;
constexpr uint8_t OP_ROTATE_CELL = 0x61;
constexpr uint8_t OP_TOGGLE_CELL = 0x62;
constexpr uint8_t OP_REMOVE_CELL = 0x63;

// Reproduction (0x80-0x9F)
constexpr uint8_t OP_START_MATE_SEARCH = 0x80;
constexpr uint8_t OP_ADD_MATE_WEIGHT = 0x81;
constexpr uint8_t OP_FINISH_MATE_SELECT = 0x82;
constexpr uint8_t OP_LAUNCH_SEED = 0x83;

// Mate selection criteria codes
constexpr uint8_t MATE_CRITERION_SIZE = 0x00;
constexpr uint8_t MATE_CRITERION_AGE = 0x01;
constexpr uint8_t MATE_CRITERION_ENERGY = 0x02;
constexpr uint8_t MATE_CRITERION_WATER = 0x03;
constexpr uint8_t MATE_CRITERION_NUTRIENTS = 0x04;
constexpr uint8_t MATE_CRITERION_DISTANCE = 0x05;
constexpr uint8_t MATE_CRITERION_SIMILARITY = 0x06;
constexpr uint8_t MATE_CRITERION_DIFFERENCE = 0x07;

}  // namespace pbg
