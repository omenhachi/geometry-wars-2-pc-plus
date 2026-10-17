#pragma once

#include <cstdint>

struct PPCContext;

namespace rex::system {
class IInputSystem;
}

namespace gw2::rumble {

void RequestBombPulse(uint32_t user, uint32_t caller);
void RequestExtraLifePulse(uint32_t user, uint32_t caller);
void RequestExtraBombPulse(uint32_t user, uint32_t caller);
void RequestShieldHitPulse(uint32_t user, uint32_t caller);
void RequestGravityOverloadPulse(uint32_t caller);
void RequestGravityExplosionPulse(uint32_t caller);
void RequestGatePulse(uint32_t gate, uint32_t score_id, uint32_t caller);
void ObserveKingZone(float x, float y, float radius, float fraction);
void ObserveLocalPlayer(float x, float y, float radius);
void PlayerKilled(uint32_t user, bool final_life, bool game_over_possible, uint32_t caller);
void PlayerSpawned(uint32_t user, uint32_t caller);
void ServiceFrame(PPCContext& ctx, uint8_t* base);
void Shutdown(rex::system::IInputSystem* input_system);

}  // namespace gw2::rumble
