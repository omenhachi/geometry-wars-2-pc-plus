#include "run_stats.h"

#include <algorithm>
#include <format>

namespace gw2::run_stats {

void Run::Start(uint32_t mode, float time) {
  *this = Run{};
  active_ = true;
  mode_ = mode;
  samples_.push_back({time, 0});
}

void Run::Kill(float time, int32_t type, int32_t killer, int64_t points, uint32_t killer_id) {
  if (!active_) return;
  TypeTally& tally = by_type_[type];
  ++tally.kills;
  tally.points += points;
  kill_points_ += points;
  if (type == kBlackHole) black_hole_points_ += points;
  if (type == kGate || killer == kGateBlast) gate_points_ += points;
  if (points > best_.points) best_ = {time, type, points};
  if (killer == kGateBlast) {
    // A different blast object, or the same one a second later: the game may reuse a blast for the next gate.
    if (killer_id != blast_id_ || killer_id == 0 || time - blast_time_ > 1.0f) blast_ = {};
    blast_id_ = killer_id;
    blast_time_ = time;
    ++blast_.kills;
    blast_.points += points;
    if (blast_.points > best_blast_.points) best_blast_ = blast_;
  }
}

void Run::Death(float time) {
  if (active_) deaths_.push_back(time);
}

void Run::Bomb(float time) {
  if (active_) bombs_.push_back(time);
}

void Run::Observe(float time, int64_t score) {
  if (!active_) return;
  if (samples_.size() >= 2 && time - samples_[samples_.size() - 2].time < kSampleSeconds) {
    samples_.back() = {time, score};  // the latest score, until the next sample is due
  } else {
    samples_.push_back({time, score});
  }
}

void Run::Finish() {
  active_ = false;
}

std::vector<TypeRow> Run::RankedEnemies() const {
  std::vector<TypeRow> rows;
  for (const auto& [type, tally] : by_type_) {
    if (type != kBlackHole && type != kGate) rows.push_back({type, tally});
  }
  std::stable_sort(rows.begin(), rows.end(), [](const TypeRow& a, const TypeRow& b) {
    return a.tally.points != b.tally.points ? a.tally.points > b.tally.points : a.tally.kills > b.tally.kills;
  });
  return rows;
}

std::vector<std::string> Run::Describe() const {
  std::vector<std::string> lines;
  for (const auto& [type, tally] : by_type_) {
    lines.push_back(std::format("type {:>2}: {:>5} kills, {:>10} points", type, tally.kills, tally.points));
  }
  lines.push_back(std::format("black holes {} points, gates {} points, best moment type {} for {} at {:.1f}s",
                              black_hole_points_, gate_points_, best_.type, best_.points, best_.time));
  lines.push_back(std::format("biggest gate blast: {} kills for {} points", best_blast_.kills, best_blast_.points));
  lines.push_back(std::format("final score {}, kill points {}, other {}, {} deaths, {} bombs, {} samples, {:.1f}s",
                              final_score(), kill_points_, final_score() - kill_points_, deaths_.size(), bombs_.size(),
                              samples_.size(), samples_.empty() ? 0.0f : samples_.back().time));
  return lines;
}

}  // namespace gw2::run_stats
