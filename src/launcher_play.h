#pragma once

#include <functional>

namespace gw2 {

enum class PlayRequest { kStarting, kBusy, kFilesNotReady, kSaveFailed };

// PLAY's gate. An accepted request saves the settings once; a failed save keeps the launcher open and nothing starts.
class PlayGate {
 public:
  PlayRequest Request(bool busy, bool files_ready, const std::function<bool()>& save) {
    if (starting_ || busy) {
      return PlayRequest::kBusy;
    }
    if (!files_ready) {
      return PlayRequest::kFilesNotReady;
    }
    if (!save()) {
      return PlayRequest::kSaveFailed;
    }
    starting_ = true;
    return PlayRequest::kStarting;
  }

  // The start did not go ahead after all, for example because the new window could not be created.
  void Cancel() { starting_ = false; }

  bool starting() const { return starting_; }

 private:
  bool starting_ = false;
};

}  // namespace gw2
