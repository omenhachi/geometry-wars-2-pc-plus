#pragma once

// Benchmark (benchmark.cpp): --gw2_benchmark=<replay file> plays that replay as soon as the menus are up and times every
// frame, writing the results to benchmark.txt.

#include <cstdint>
#include <filesystem>

namespace gw2::benchmark {

void SetResultsFile(std::filesystem::path file);
// Every frame (the pad poll, demo.cpp).
void EachFrame(const uint8_t* base);

}  // namespace gw2::benchmark
