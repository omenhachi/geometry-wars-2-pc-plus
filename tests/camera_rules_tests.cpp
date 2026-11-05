// Chase camera tests: the view matrix agrees with the game's own camera, Chase keeps the arena's directions on screen
// (ship centred and low, up the arena up the screen, right to the right), the glide and the follow neither jump nor
// overshoot, and mouse aim finds the floor under the cursor even above the horizon. Pure; no game.

#include <cmath>
#include <cstdio>
#include <initializer_list>
#include <utility>

#include "aim_camera.h"
#include "camera_rules.h"

using namespace gw2::camera;
namespace aim = gw2::aim;

namespace {

int g_failures = 0;
int g_checks = 0;

#define CHECK(cond)                                                 \
  do {                                                              \
    ++g_checks;                                                     \
    if (!(cond)) {                                                  \
      ++g_failures;                                                 \
      std::printf("  FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); \
    }                                                               \
  } while (0)

bool Near(float a, float b, float tolerance = 1e-3f) {
  return std::fabs(a - b) <= tolerance;
}

// A D3D left-handed perspective projection, as the game's might be.
aim::Mat4 Perspective(float fov_y_degrees, float aspect, float zn, float zf) {
  const float ys = 1.0f / std::tan(fov_y_degrees * 0.5f * 3.14159265f / 180.0f);
  const float xs = ys / aspect;
  return {xs, 0, 0, 0, 0, ys, 0, 0, 0, 0, zf / (zf - zn), 1, 0, 0, -zn * zf / (zf - zn), 0};
}

// The game's own gameplay camera, as logged: 512 above the floor looking down, +x right, +y down the screen.
const Mat4 kClassicWorld = {1, 0, 0, 0, 0, -1, 0, 0, 0, 0, -1, 0, 0, 0, 512, 1};

void ClassicView() {
  const Mat4 view = ViewFrom(PoseFromWorld(kClassicWorld));
  const aim::Mat4 proj = Perspective(60, 16.0f / 9.0f, 10, 5000);
  // The arena's centre is the middle of the screen; +x is to the right and +y is down the screen.
  const auto centre = aim::ProjectToNdc(view, proj, 0, 0, 0);
  const auto right = aim::ProjectToNdc(view, proj, 100, 0, 0);
  const auto down = aim::ProjectToNdc(view, proj, 0, 100, 0);
  CHECK(centre && Near(centre->x, 0) && Near(centre->y, 0));
  CHECK(right && right->x > 0 && Near(right->y, 0));
  CHECK(down && down->y < 0 && Near(down->x, 0));
  // Looking straight down every point has floor under it, so the clamped unprojection is the plain one.
  const auto plain = aim::UnprojectToArena(view, proj, {0.5f, 0.9f});
  const auto clamped = aim::UnprojectToArenaBelowHorizon(view, proj, {0.5f, 0.9f});
  CHECK(plain && clamped && Near(plain->x, clamped->x) && Near(plain->y, clamped->y));
}

void ChaseView() {
  const ChaseTuning tuning;
  const Mat4 view = ViewFrom(ChasePose(40, -30, tuning));
  const aim::Mat4 proj = Perspective(60, 16.0f / 9.0f, 10, 5000);
  const auto ship = aim::ProjectToNdc(view, proj, 40, -30, 0);
  const auto ahead = aim::ProjectToNdc(view, proj, 40, -130, 0);
  const auto right = aim::ProjectToNdc(view, proj, 90, -30, 0);
  CHECK(ship && Near(ship->x, 0) && ship->y < 0 && ship->y > -1);  // centred, in the lower half, on screen
  CHECK(ahead && ahead->y > ship->y);                              // up the arena is up the screen
  CHECK(right && right->x > 0);                                    // right is still right
  // The floor under a point on screen comes back as the point that was projected.
  const auto floor = aim::UnprojectToArena(view, proj, *right);
  CHECK(floor && Near(floor->x, 90, 0.05f) && Near(floor->y, -30, 0.05f));
}

void Horizon() {
  // A low camera with a wide view (the shipped one is steep enough never to see the horizon; this is the safety net).
  const Mat4 view = ViewFrom(ChasePose(0, 0, ChaseTuning{.behind = 300, .height = 250, .ahead = 170}));
  const aim::Mat4 proj = Perspective(60, 16.0f / 9.0f, 10, 5000);
  // The top of this screen is above the horizon: its line meets the floor behind the eye, which would aim backwards.
  const auto wrong = aim::UnprojectToArena(view, proj, {0.3f, 1.0f});
  CHECK(!wrong || !aim::ProjectToNdc(view, proj, wrong->x, wrong->y, 0));
  const auto aimed = aim::UnprojectToArenaBelowHorizon(view, proj, {0.3f, 1.0f});
  CHECK(aimed.has_value());
  if (aimed) {
    CHECK(aimed->y < -500);  // far up the arena, ahead of the ship
    CHECK(aimed->x > 0);     // and still to the right, as the cursor is
  }
}

void Glide() {
  CHECK(Ease(0) == 0 && Ease(1) == 1 && Near(Ease(0.5f), 0.5f));
  const Pose classic = PoseFromWorld(kClassicWorld);
  const Pose chase = ChasePose(0, 0, ChaseTuning{});
  CHECK(Near(Blend(classic, chase, 0).eye.z, 512) && Near(Blend(classic, chase, 1).eye.z, chase.eye.z));
  // Over 0.4 s at 60 frames a second the blend reaches 1, never passes it, and comes back down the same way.
  float blend = 0;
  int frames = 0;
  while (blend < 1 && frames < 100) {
    blend = StepBlend(blend, 1, 1.0f / 60, 0.4f);
    ++frames;
  }
  CHECK(blend == 1 && frames == 24);
  CHECK(StepBlend(1, 1, 0.5f, 0.4f) == 1);
  CHECK(StepBlend(1, 0, 0.5f, 0.4f) == 0);
  // Mid-glide the view is still a proper camera: nothing degenerate between straight down and tilted.
  for (float t = 0; t <= 1.0f; t += 0.125f) {
    const Mat4 view = ViewFrom(Blend(classic, chase, t));
    const auto ship = aim::ProjectToNdc(view, Perspective(60, 16.0f / 9.0f, 10, 5000), 0, 0, 0);
    CHECK(ship && std::isfinite(ship->y) && Near(ship->x, 0));
  }
}

void Following() {
  Follow follow;
  follow.Step(10, 20, 1.0f / 60, 9);
  CHECK(follow.x == 10 && follow.y == 20);  // the first frame is placed on the ship
  float last = follow.x;
  bool overshoot = false, backwards = false;
  for (int i = 0; i < 240; ++i) {
    follow.Step(200, 20, 1.0f / 60, 9);
    overshoot |= follow.x > 200.001f;
    backwards |= follow.x < last - 1e-4f;
    last = follow.x;
  }
  CHECK(!overshoot && !backwards);
  CHECK(Near(follow.x, 200, 0.5f));
  follow.Snap(-50, 7);
  CHECK(follow.x == -50 && follow.y == 7 && follow.vx == 0);
  // A long stall (a breakpoint, a hitch) doesn't fling it.
  follow.Step(0, 7, 5.0f, 9);
  CHECK(follow.x <= 0.001f && follow.x >= -50);
}

void Turning() {
  // Facing up the arena leaves the sticks exactly as they are.
  for (const auto [x, y] : {std::pair<int16_t, int16_t>{0, 32767}, {-32768, 0}, {12000, -500}, {0, 0}}) {
    const StickXY same = TurnStick(x, y, 0.0f, -1.0f);
    CHECK(same.x == x && same.y == y);
  }
  // Facing down the arena: up on the stick is down the arena, which is what the screen's up now shows.
  const StickXY down = TurnStick(0, 32767, 0.0f, 1.0f);
  CHECK(down.x == 0 && down.y == -32767);
  const StickXY right_when_down = TurnStick(32767, 0, 0.0f, 1.0f);
  CHECK(right_when_down.x == -32767 && right_when_down.y == 0);  // screen right is the arena's left
  // Facing right (+x): up on the stick is +x on the arena.
  const StickXY east = TurnStick(0, 32767, 1.0f, 0.0f);
  CHECK(east.x == 32767 && east.y == 0);
  // A diagonal never wraps past the int16 range.
  const float d = 0.70710678f;
  const StickXY corner = TurnStick(32767, 32767, d, -d);
  CHECK(corner.x >= -32768 && corner.y <= 32767);

  CHECK(Near(AngleDelta(3.0f, -3.0f), 0.2831853f));  // across the wrap, the short way
  CHECK(Near(AngleDelta(0.0f, 12.566371f + 0.5f), 0.5f));
  YawFollow yaw;
  yaw.yaw = 0.0f;
  float last = 0.0f;
  bool too_fast = false;
  for (int i = 0; i < 120; ++i) {
    yaw.Step(3.1f, 1.0f / 60, 3.0f, 3.2f);
    too_fast |= std::fabs(yaw.yaw - last) > 3.2f / 60 + 1e-5f;
    last = yaw.yaw;
  }
  CHECK(!too_fast);
  CHECK(Near(yaw.yaw, 3.1f, 0.05f));

  // Into the arena: from the top wall it faces down, from the left wall it faces right; near the middle it holds.
  const auto from_top = FaceArenaYaw(0, -190);
  const auto from_left = FaceArenaYaw(-290, 0);
  CHECK(from_top && Near(*from_top, 1.5707963f));
  CHECK(from_left && Near(*from_left, 0.0f));
  CHECK(!FaceArenaYaw(20, 30));

  // A Chase facing down the arena: the ship is still centred and low, and further down the arena is further up the
  // screen; the stick turned for that camera moves the ship up the screen.
  const aim::Mat4 proj = Perspective(36, 16.0f / 9.0f, 10, 5000);
  const Mat4 view = ViewFrom(ChasePose(0, -150, ChaseTuning{}, 1.5707963f));
  const auto ship = aim::ProjectToNdc(view, proj, 0, -150, 0);
  const auto further = aim::ProjectToNdc(view, proj, 0, -50, 0);
  CHECK(ship && Near(ship->x, 0) && ship->y < 0 && ship->y > -1);
  CHECK(further && further->y > ship->y);
  // Screen up on the arena (the view's up column) is +y here, as TurnSticks reads it.
  CHECK(Near(view[1], 0) && view[5] > 0.5f);
  // The shipped tuning shows at least 150 of floor behind the ship and the ship below the middle.
  const Mat4 up_view = ViewFrom(ChasePose(0, 0, ChaseTuning{}));
  const auto behind = aim::UnprojectToArena(up_view, proj, {0, -1});
  const auto ship_up = aim::ProjectToNdc(up_view, proj, 0, 0, 0);
  CHECK(behind && behind->y >= 150);
  CHECK(ship_up && ship_up->y < 0 && ship_up->y > -0.4f);
}

void Latching() {
  StickLatch latch;
  // Pushed up while the camera faces up the arena: up the arena.
  StickXY s = latch.Apply(0, 32767, 0.0f, -1.0f);
  CHECK(latch.held && s.x == 0 && s.y == 32767);
  // Still held while the camera turns to face +x: it keeps moving up the arena, not along +x.
  s = latch.Apply(0, 32767, 1.0f, 0.0f);
  CHECK(s.x == 0 && s.y == 32767);
  // Changing keys without letting go (W to D) stays on the latched facing: D is the arena's right, as it was.
  s = latch.Apply(32767, 0, 1.0f, 0.0f);
  CHECK(latch.held && s.x == 32767 && s.y == 0);
  // Let go: the latch drops, and the next push takes the camera as it is now (+x).
  s = latch.Apply(0, 0, 1.0f, 0.0f);
  CHECK(!latch.held && s.x == 0 && s.y == 0);
  s = latch.Apply(0, 32767, 1.0f, 0.0f);
  CHECK(latch.held && s.x == 32767 && s.y == 0);
  // A stick resting just inside the dead zone doesn't latch.
  StickLatch small;
  small.Apply(5000, 5000, 1.0f, 0.0f);
  CHECK(!small.held);
}

void Konami() {
  using P = Press;
  const auto enter = [](KonamiCode& code, double& t, std::initializer_list<P> presses) {
    bool done = false;
    for (const P p : presses) {
      t += 0.3;
      done = code.Feed(p, t);
    }
    return done;
  };
  const std::initializer_list<P> full = {P::kUp,    P::kUp,   P::kDown,  P::kDown, P::kLeft,
                                         P::kRight, P::kLeft, P::kRight, P::kX,    P::kY};
  KonamiCode code;
  double t = 0;
  CHECK(enter(code, t, full));
  CHECK(enter(code, t, full));  // again, straight after
  // B A (the original ending) doesn't do it.
  CHECK(!enter(code, t, {P::kUp, P::kUp, P::kDown, P::kDown, P::kLeft, P::kRight, P::kLeft, P::kRight, P::kOther, P::kOther}));
  // A stray press starts again; an extra Up at the start still counts.
  CHECK(!enter(code, t, {P::kUp, P::kUp, P::kDown, P::kX}));
  CHECK(enter(code, t, full));
  CHECK(enter(code, t, {P::kUp, P::kUp, P::kUp, P::kDown, P::kDown, P::kLeft, P::kRight, P::kLeft, P::kRight, P::kX, P::kY}));
  // Too slow between presses starts again.
  KonamiCode slow;
  double s = 0;
  CHECK(!enter(slow, s, {P::kUp, P::kUp, P::kDown, P::kDown}));
  s += 5.0;
  CHECK(!enter(slow, s, {P::kLeft, P::kRight, P::kLeft, P::kRight, P::kX, P::kY}));
  CHECK(enter(slow, s, full));
  // The views go round.
  CHECK(NextView(SecretView::kClassic) == SecretView::kChase);
  CHECK(NextView(SecretView::kChase) == SecretView::kFirstPerson);
  CHECK(NextView(SecretView::kFirstPerson) == SecretView::kClassic);
}

}  // namespace

int main() {
  ClassicView();
  ChaseView();
  Horizon();
  Glide();
  Following();
  Turning();
  Latching();
  Konami();
  std::printf("camera_rules_tests: %d checks, %d failures\n", g_checks, g_failures);
  return g_failures == 0 ? 0 : 1;
}
