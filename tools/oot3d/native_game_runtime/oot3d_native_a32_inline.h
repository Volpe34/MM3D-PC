#pragma once

// Rare paths of the guest memory and block-entry helpers that generated code
// calls on every load, store and basic block: kept out of line so the common
// path needs no stack frame.
#if defined(__GNUC__) || defined(__clang__)
#define OOT3D_A32_COLD __attribute__((noinline, cold))
#elif defined(_MSC_VER)
#define OOT3D_A32_COLD __declspec(noinline)
#else
#define OOT3D_A32_COLD
#endif
