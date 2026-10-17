#pragma once

#include <cstdint>

struct PPCContext;

namespace rex::system {
class IInputSystem;
}

namespace gw2::rumble {

void RequestBombPulse(uint32_t user);
void RequestExtraLifePulse(uint32_t user);
void RequestExtraBombPulse(uint32_t user);
void RequestShieldHitPulse(uint32_t user);
void RequestGravityOverloadPulse();
void RequestGravityExplosionPulse();
void RequestGatePulse();
void ObserveKingZone(float x, float y, float radius, float fraction);
void ObserveLocalPlayer(float x, float y, float radius);
void PlayerKilled(uint32_t user, bool final_life, bool game_over_possible);
void PlayerSpawned(uint32_t user);
void ServiceFrame(PPCContext& ctx, uint8_t* base);
void Shutdown(rex::system::IInputSystem* input_system);

}  // namespace gw2::rumble
