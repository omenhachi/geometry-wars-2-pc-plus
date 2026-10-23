#pragma once

// One single-player run's story, for the results screen: kills and points by enemy type, deaths and bombs, and the score
// over the run's own clock (game time, so pauses leave no gap). Fed by run_stats_hooks.cpp; pure, so it is unit tested.

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace gw2::run_stats {

// The game's object types (an object's word at +68), as the kill award (sub_820408D0) and K weights name them.
inline constexpr int32_t kBlackHole = 3;
inline constexpr int32_t kGate = 24;       // flying through a gate kills it, credited to the ship
inline constexpr int32_t kGateBlast = 15;  // a gate's explosion, the killer of everything caught in it

struct TypeTally {
  int32_t kills = 0;
  int64_t points = 0;
};

struct TypeRow {
  int32_t type = 0;
  TypeTally tally;
};

struct Moment {
  float time = 0;
  int32_t type = 0;
  int64_t points = 0;
};

// One gate's explosion: everything it caught.
struct Blast {
  int32_t kills = 0;
  int64_t points = 0;
};

struct Sample {
  float time = 0;
  int64_t score = 0;
};

class Run {
 public:
  static constexpr float kSampleSeconds = 0.25f;

  void Start(uint32_t mode, float time);
  bool active() const { return active_; }
  uint32_t mode() const { return mode_; }

  // A kill credited to the player: the enemy's type, the type of what killed it, and the score it added. `killer_id`
  // tells one killer object from another (its address), so a gate blast's kills group together.
  void Kill(float time, int32_t type, int32_t killer, int64_t points, uint32_t killer_id = 0);
  void Death(float time);
  void Bomb(float time);
  // Called every frame; keeps a sample every kSampleSeconds, and always the latest score.
  void Observe(float time, int64_t score);
  void Finish();

  const std::map<int32_t, TypeTally>& by_type() const { return by_type_; }
  // Points from destroying black holes, and from gates: gate kills plus everything caught in a gate's blast.
  int64_t black_hole_points() const { return black_hole_points_; }
  int64_t gate_points() const { return gate_points_; }
  int64_t kill_points() const { return kill_points_; }
  int64_t final_score() const { return samples_.empty() ? 0 : samples_.back().score; }
  const std::vector<Sample>& samples() const { return samples_; }
  const std::vector<float>& deaths() const { return deaths_; }
  const std::vector<float>& bombs() const { return bombs_; }
  const Moment& best() const { return best_; }
  const Blast& best_blast() const { return best_blast_; }
  // How long the run has lasted, on its own clock.
  float duration() const { return samples_.empty() ? 0.0f : samples_.back().time - samples_.front().time; }
  // The enemy types killed, most points first (then most kills), without black holes and gates: they have their own
  // rows.
  std::vector<TypeRow> RankedEnemies() const;

  // A one-line summary per type and the totals, for the log.
  std::vector<std::string> Describe() const;

 private:
  bool active_ = false;
  uint32_t mode_ = 0;
  std::map<int32_t, TypeTally> by_type_;
  int64_t black_hole_points_ = 0;
  int64_t gate_points_ = 0;
  int64_t kill_points_ = 0;
  std::vector<Sample> samples_;
  std::vector<float> deaths_;
  std::vector<float> bombs_;
  Moment best_;
  uint32_t blast_id_ = 0;
  float blast_time_ = 0;
  Blast blast_;
  Blast best_blast_;
};

}  // namespace gw2::run_stats
