// I believe some of the things here don't work
#pragma once

#include <cstddef>
#include <cstdint>

namespace sw::physics {

struct Vec3 {
    double x = 0, y = 0, z = 0;
};

// Hooks the step function of the world (0x133e90). False if the game does not match,
// the hook fails, or this is the 32 bit build.
bool watch();

// A world was stepped in the last moments.
bool ready();

// The gravity of the world (m_gravity, meters per second squared; (0, -10, 0) by default).
bool gravity(Vec3& out);

// Asks for this gravity; it is written at the next step. Refuses a value that is not
// finite or larger than 1000 in any component.
bool set_gravity(const Vec3& value);

// The solver iterations of the world (10 by default).
bool solver_iterations(int& out);

// Asks for this number of iterations (1 to 256); it is written at the next step.
bool set_solver_iterations(int iterations);

// The fixed time step the game passed to the last step (m_fixedTimeStep, seconds),
// and the time of the last step that has not been simulated yet (m_localTime).
bool fixed_time_step(double& out);
bool local_time(double& out);

// The number of steps seen since watch(). Divided by the time it is the physics rate.
std::uint64_t steps();

// They need no watch(), only the attached runtime; the code is checked first (the
// opcodes and that the displacements point at the two constants), so a game that does
// not match is refused. The change takes effect at the next call of the step.

// The fixed time step of one internal step (1/180 s in the game), 1/2000 to 1/30.
bool set_fixed_time_step(double seconds);
// The time one call of the physics step advances (1/60 s in the game), 1/480 to 1/10.
// A larger value runs the physics faster than the game clock, a smaller one slower.
bool set_step_time(double seconds);
// The most internal steps of one call (1000 in the game), 1 to 100000.
bool set_max_sub_steps(int steps);
// What the code uses now. False when the code does not match.
bool step_time(double& out);
bool max_sub_steps(int& out);
// Puts the three constants back to the values of the game.
bool restore_time_steps();

// Stops forcing the values of set_gravity / set_solver_iterations. What is in the world
// stays until the game changes it.
void clear_overrides();

// Checks a pointer: it is a world of the game (the vtable of the discrete or the
// soft-rigid world).
bool is_world(std::uintptr_t address);

inline constexpr std::uint32_t kVtableDiscreteWorld = 0xae6058;    // btDiscreteDynamicsWorld
inline constexpr std::uint32_t kVtableSoftRigidWorld = 0xae6710;   // btSoftRigidDynamicsWorld
inline constexpr std::uint32_t kRvaStepSimulation = 0x133e90;      // 778 bytes
inline constexpr std::uint32_t kRvaSetGravity = 0x134290;          // slot 17, a leaf without .pdata
inline constexpr std::uint32_t kRvaGetGravity = 0x134370;          // slot 18, a leaf without .pdata
inline constexpr std::uint32_t kRvaInternalSingleStep = 0x1341a0;  // slot 39, 230 bytes
inline constexpr std::uint32_t kRvaSolverInfoCtor = 0x1328a0;      // sets 10 iterations
inline constexpr std::uint32_t kRvaPhysicsSceneStep = 0x1a3bc0;    // the only caller, 138 bytes
inline constexpr std::uint32_t kRvaConstFixedStep = 0xbb3318;      // double 1/180 (read by 0x1a3bcf)
inline constexpr std::uint32_t kRvaConstStepTime = 0xbb33a0;       // double 1/60 (read by 0x1a3bda)
inline constexpr std::uint32_t kRvaImmMaxSubSteps = 0x1a3be4;      // int32 1000 in "mov r8d, imm32"
inline constexpr double kGameFixedStep = 0.0055555556900799274;   // the float 1/180 as a double
inline constexpr double kGameStepTime = 0.01666666753590107;     // the float 1/60 as a double
inline constexpr int kGameMaxSubSteps = 1000;
inline constexpr std::size_t kSlotSetGravity = 17;
inline constexpr std::size_t kSlotGetGravity = 18;
inline constexpr std::size_t kOffSolverInfo = 0xa8;
inline constexpr std::size_t kOffSolverTimeStep = 0xc0;
inline constexpr std::size_t kOffSolverIterations = 0xd0;
inline constexpr std::size_t kOffGravity = 0x1e0;
inline constexpr std::size_t kOffLocalTime = 0x200;
inline constexpr std::size_t kOffFixedTimeStep = 0x208;
inline constexpr std::size_t kOffSynchronizeMotionStates = 0x23c;

}  // namespace sw::physics
