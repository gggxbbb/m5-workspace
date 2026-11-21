#pragma once
#ifndef SDL_h_
#define SDL_h_

#include <chrono>
#include <cstdint>
#include <thread>

#define SDL_GetTicks() lz_sdl_shim_ticks()
#define SDL_GetPerformanceCounter() lz_sdl_shim_perf_counter()
#define SDL_GetPerformanceFrequency() 1000000ULL
#define SDL_Delay(ms) std::this_thread::sleep_for(std::chrono::milliseconds(ms))

inline uint32_t lz_sdl_shim_ticks() {
  static const auto t0 = std::chrono::steady_clock::now();
  return static_cast<uint32_t>(
      std::chrono::duration_cast<std::chrono::milliseconds>(
          std::chrono::steady_clock::now() - t0).count());
}

inline uint64_t lz_sdl_shim_perf_counter() {
  static const auto t0 = std::chrono::steady_clock::now();
  return static_cast<uint64_t>(
      std::chrono::duration_cast<std::chrono::microseconds>(
          std::chrono::steady_clock::now() - t0).count());
}

struct SDL_Window;
struct SDL_Renderer;
struct SDL_Texture;
struct SDL_mutex;
struct SDL_semaphore;
using SDL_Keymod = uint32_t;
using SDL_KeyCode = int32_t;
constexpr SDL_Keymod KMOD_NONE = 0;
constexpr SDL_KeyCode SDLK_UNKNOWN = 0;

#endif
