#include "cheat_manager.h"

#include "common/events/player_event_logs.h"
#include "zone/client.h"
#include "zone/quest_parser_collection.h"
#include "zone/worldserver.h"
#include "zone/queryserv.h"

#include <algorithm>

extern WorldServer worldserver;
extern QueryServ *QServ;

void CheatManager::SetClient(Client *cli)
{
	m_target = cli;
}

void CheatManager::SetExemptStatus(ExemptionType type, bool v)
{
	if (v) {
		MovementCheck();
		// Set exemption with grace period if enabled
		if (RuleB(Cheat, EnableExemptionGracePeriod)) {
			m_exemption_expiry_time[type] = Timer::GetCurrentTime() + ExemptionGracePeriodMS;
		}
	}
	m_exemption[type] = v;
}

bool CheatManager::GetExemptStatus(ExemptionType type)
{
	// Check if exemption has expired
	if (m_exemption[type] && RuleB(Cheat, EnableExemptionGracePeriod)) {
		uint32 current_time = Timer::GetCurrentTime();
		if (m_exemption_expiry_time[type] > 0 && current_time > m_exemption_expiry_time[type]) {
			// Grace period expired, clear exemption
			m_exemption[type] = false;
			m_exemption_expiry_time[type] = 0;
			return false;
		}
	}
	return m_exemption[type];
}

void CheatManager::CheatDetected(CheatTypes type, glm::vec3 position1, glm::vec3 position2)
{
	switch (type) {
		case MQWarp:
			if (m_time_since_last_warp_detection.GetRemainingTime() == 0 && RuleB(Cheat, EnableMQWarpDetector) &&
				((m_target->Admin() < RuleI(Cheat, MQWarpExemptStatus) || (RuleI(Cheat, MQWarpExemptStatus)) == -1))) {
				std::string message = fmt::format(
					"/MQWarp (large warp detection) with location from x [{:.2f}] y [{:.2f}] z [{:.2f}] to x [{:.2f}] y [{:.2f}] z [{:.2f}] Distance [{:.2f}]",
					position1.x,
					position1.y,
					position1.z,
					position2.x,
					position2.y,
					position2.z,
					Distance(position1, position2)
				);

				RecordPlayerEventLogWithClient(m_target, PlayerEvent::POSSIBLE_HACK, PlayerEvent::PossibleHackEvent{.message = message});

				LogCheat(fmt::runtime(message));

				if (parse->PlayerHasQuestSub(EVENT_WARP)) {
					const auto& export_string = fmt::format(
						"{} {} {}",
						position1.x,
						position1.y,
						position1.z
					);

					parse->EventPlayer(EVENT_WARP, m_target, export_string, 0);
				}

				m_time_since_last_warp_detection.Start(WarpDetectionCooldownMS);
			}
			break;
	case MQWarpAbsolute:
		// Apply cooldown to reduce false positives if enabled
		if ((RuleB(Cheat, EnableMQAbsoluteWarpCooldown) && m_time_since_last_absolute_warp_detection.GetRemainingTime() > 0)) {
			break;
		}
		if (RuleB(Cheat, EnableMQWarpDetector) &&
			((m_target->Admin() < RuleI(Cheat, MQWarpExemptStatus) || (RuleI(Cheat, MQWarpExemptStatus)) == -1))) {
			std::string message = fmt::format(
				"/MQWarp (Absolute) with location from x [{:.2f}] y [{:.2f}] z [{:.2f}] to x [{:.2f}] y [{:.2f}] z [{:.2f}] Distance [{:.2f}]",
				position1.x,
				position1.y,
				position1.z,
				position2.x,
				position2.y,
				position2.z,
				Distance(position1, position2)
			);
			RecordPlayerEventLogWithClient(m_target, PlayerEvent::POSSIBLE_HACK, PlayerEvent::PossibleHackEvent{.message = message});
			LogCheat(fmt::runtime(message));

			if (parse->PlayerHasQuestSub(EVENT_WARP)) {
				const auto& export_string = fmt::format(
					"{} {} {}",
					position1.x,
					position1.y,
					position1.z
				);

				parse->EventPlayer(EVENT_WARP, m_target, export_string, 0);
			}

			m_time_since_last_warp_detection.Start(WarpDetectionCooldownMS);
			m_time_since_last_absolute_warp_detection.Start(AbsoluteWarpCooldownMS);
		}
		break;
		case MQWarpShadowStep:
			if (RuleB(Cheat, EnableMQWarpDetector) &&
				((m_target->Admin() < RuleI(Cheat, MQWarpExemptStatus) || (RuleI(Cheat, MQWarpExemptStatus)) == -1))) {
				std::string message = fmt::format(
					"/MQWarp (ShadowStep) with location from x [{:.2f}] y [{:.2f}] z [{:.2f}] the target was shadow step exempt but we still found this suspicious.",
					position1.x,
					position1.y,
					position1.z
				);
				RecordPlayerEventLogWithClient(m_target, PlayerEvent::POSSIBLE_HACK, PlayerEvent::PossibleHackEvent{.message = message});
				LogCheat(fmt::runtime(message));
			}
			break;
		case MQWarpKnockBack:
			if (RuleB(Cheat, EnableMQWarpDetector) &&
				((m_target->Admin() < RuleI(Cheat, MQWarpExemptStatus) || (RuleI(Cheat, MQWarpExemptStatus)) == -1))) {
				std::string message = fmt::format(
					"/MQWarp (Knockback) with location from x [{:.2f}] y [{:.2f}] z [{:.2f}] the target was Knock Back exempt but we still found this suspicious.",
					position1.x,
					position1.y,
					position1.z
				);
				RecordPlayerEventLogWithClient(m_target, PlayerEvent::POSSIBLE_HACK, PlayerEvent::PossibleHackEvent{.message = message});
				LogCheat(fmt::runtime(message));
			}
			break;

		case MQWarpLight:
			if (RuleB(Cheat, EnableMQWarpDetector) &&
				((m_target->Admin() < RuleI(Cheat, MQWarpExemptStatus) || (RuleI(Cheat, MQWarpExemptStatus)) == -1))) {
				if (RuleB(Cheat, MarkMQWarpLT)) {
					std::string message = fmt::format(
						"/MQWarp (light) with location from x [{:.2f}] y [{:.2f}] z [{:.2f}] to x [{:.2f}] y [{:.2f}] z [{:.2f}] Distance [{:.2f}] running fast but not fast enough to get killed, possibly: small warp, speed hack, excessive lag, marked as suspicious.",
						position1.x,
						position1.y,
						position1.z,
						position2.x,
						position2.y,
						position2.z,
						Distance(position1, position2)
					);
					RecordPlayerEventLogWithClient(m_target, PlayerEvent::POSSIBLE_HACK, PlayerEvent::PossibleHackEvent{.message = message});
					LogCheat(fmt::runtime(message));
				}
			}
			break;

		case MQZone:
			if (RuleB(Cheat, EnableMQZoneDetector) &&
				((m_target->Admin() < RuleI(Cheat, MQZoneExemptStatus) || (RuleI(Cheat, MQZoneExemptStatus)) == -1))) {
				std::string message = fmt::format(
					"/MQZone used at x [{:.2f}] y [{:.2f}] z [{:.2f}]",
					position1.x,
					position1.y,
					position1.z
				);
				RecordPlayerEventLogWithClient(m_target, PlayerEvent::POSSIBLE_HACK, PlayerEvent::PossibleHackEvent{.message = message});
				LogCheat(fmt::runtime(message));
			}
			break;
		case MQZoneUnknownDest:
			if (RuleB(Cheat, EnableMQZoneDetector) &&
				((m_target->Admin() < RuleI(Cheat, MQZoneExemptStatus) || (RuleI(Cheat, MQZoneExemptStatus)) == -1))) {
				std::string message = fmt::format(
					"/MQZone used at x [{:.2f}] y [{:.2f}] z [{:.2f}] with Unknown Destination",
					position1.x,
					position1.y,
					position1.z
				);
				RecordPlayerEventLogWithClient(m_target, PlayerEvent::POSSIBLE_HACK, PlayerEvent::PossibleHackEvent{.message = message});
				LogCheat(fmt::runtime(message));
			}
			break;
		case MQGate:
			if (RuleB(Cheat, EnableMQGateDetector) &&
				((m_target->Admin() < RuleI(Cheat, MQGateExemptStatus) || (RuleI(Cheat, MQGateExemptStatus)) == -1))) {
				std::string message = fmt::format(
					"/MQGate used at x [{:.2f}] y [{:.2f}] z [{:.2f}]",
					position1.x,
					position1.y,
					position1.z
				);
				RecordPlayerEventLogWithClient(m_target, PlayerEvent::POSSIBLE_HACK, PlayerEvent::PossibleHackEvent{.message = message});
				LogCheat(fmt::runtime(message));
			}
			break;
		case MQGhost:
			// this isn't just for ghost, its also for if a person isn't sending their MovementHistory packet also.
			if (RuleB(Cheat, EnableMQGhostDetector) &&
				((m_target->Admin() < RuleI(Cheat, MQGhostExemptStatus) ||
				  (RuleI(Cheat, MQGhostExemptStatus)) == -1))) {
				std::string message = fmt::format(
					"[MQGhost] [{}] [{}] was caught not sending the proper packets as regularly as they were suppose to.",
					m_target->AccountName(),
					m_target->GetName()
				);
				RecordPlayerEventLogWithClient(m_target, PlayerEvent::POSSIBLE_HACK, PlayerEvent::PossibleHackEvent{.message = message});
				LogCheat("{}", message);
			}
			break;
		case MQFastMem:
			if (RuleB(Cheat, EnableMQFastMemDetector) &&
				((m_target->Admin() < RuleI(Cheat, MQFastMemExemptStatus) ||
				  (RuleI(Cheat, MQFastMemExemptStatus)) == -1))) {
				std::string message = fmt::format(
					"/MQFastMem used at x [{:.2f}] y [{:.2f}] z [{:.2f}]",
					position1.x,
					position1.y,
					position1.z
				);
				RecordPlayerEventLogWithClient(m_target, PlayerEvent::POSSIBLE_HACK, PlayerEvent::PossibleHackEvent{.message = message});
				LogCheat(fmt::runtime(message));
			}
			break;
		default:
			std::string message = fmt::format(
				"Unhandled HackerDetection flag with location from x [{:.2f}] y [{:.2f}] z [{:.2f}]",
				position1.x,
				position1.y,
				position1.z
			);
			RecordPlayerEventLogWithClient(m_target, PlayerEvent::POSSIBLE_HACK, PlayerEvent::PossibleHackEvent{.message = message});
			LogCheat(fmt::runtime(message));
			break;
	}
}

void CheatManager::MovementCheck(glm::vec3 updated_position)
{
	if (m_time_since_last_movement_history.GetRemainingTime() == 0) {
		CheatDetected(MQGhost, updated_position);
	}

	glm::vec3 current_position = glm::vec3(m_target->GetPosition());
	float  dist     = DistanceNoZ(current_position, updated_position);
	uint32 cur_time = Timer::GetCurrentTime();
	if (dist == 0) {
		if (m_distance_since_last_position_check > 0.0f) {
			m_current_position_check_location = updated_position;
			MovementCheck(0);
		}
		else {
			m_time_since_last_position_check = cur_time;
			m_last_position_check_location   = updated_position;
			m_current_position_check_location = updated_position;
			m_cheat_detect_moved             = false;
		}
	}
	else {
		m_distance_since_last_position_check += dist;
		m_current_position_check_location = updated_position;
		m_cheat_detect_moved = true;
		if (m_time_since_last_position_check == 0) {
			m_time_since_last_position_check = cur_time;
			m_last_position_check_location   = current_position;
		}
		else {
			MovementCheck(PositionCheckIntervalMS);
		}
	}
}

void CheatManager::MovementCheck(uint32 time_between_checks)
{
	uint32 cur_time = Timer::GetCurrentTime();
	if ((cur_time - m_time_since_last_position_check) > time_between_checks) {
		float estimated_speed =
				  (m_distance_since_last_position_check * 100) / (float) (cur_time - m_time_since_last_position_check);

		// MQWarpDetection shouldn't go below 1.0f so we can't end up dividing by 0.
		float run_speed = m_target->GetRunspeed() /
						  std::max(
							  RuleR(Cheat, MQWarpDetectionDistanceFactor),
							  1.0f
						  );
		if (estimated_speed > run_speed) {
			bool using_gm_speed = m_target->GetGMSpeed();
			bool is_immobile    = m_target->GetRunspeed() == 0; // this covers stuns, roots, mez, and pseudorooted.
			const auto from      = m_last_position_check_location;
			const auto to        = m_current_position_check_location;
			
			// Check for significant vertical movement which may indicate legitimate falling/levitation
			float z_diff = std::abs(to.z - from.z);
			bool significant_z_movement = z_diff > RuleR(Cheat, MQWarpZThreshold);
			
			if (!using_gm_speed && !is_immobile) {
				if (GetExemptStatus(ShadowStep)) {
					// Use configurable threshold for shadowstep
					if (m_distance_since_last_position_check > RuleR(Cheat, MQWarpShadowStepThreshold)) {
						CheatDetected(
							MQWarpShadowStep,
							from,
							to
						);
					}
				}
				else if (GetExemptStatus(KnockBack)) {
					// Use configurable threshold for knockback
					if (estimated_speed > RuleR(Cheat, MQWarpKnockBackThreshold)) {
						CheatDetected(MQWarpKnockBack, from, to);
					}
				}
				else if (!GetExemptStatus(Port)) {
					// If significant vertical movement, be more lenient with horizontal speed checks
					// This helps with falling/levitation ending scenarios
					float speed_multiplier = significant_z_movement ? 2.0f : 1.5f;
					
					if (estimated_speed > (run_speed * speed_multiplier)) {
						CheatDetected(MQWarp, from, to);
						m_time_since_last_position_check     = cur_time;
						m_last_position_check_location       = to;
						m_distance_since_last_position_check = 0.0f;
					}
					else {
						// Don't mark as light warp if there's significant vertical movement
						if (!significant_z_movement) {
							CheatDetected(MQWarpLight, from, to);
						}
					}
				}
			}
		}
		// Only clear exemptions if grace period is disabled or time_between_checks is default
		if (time_between_checks != DefaultMovementCheckIntervalMS && !RuleB(Cheat, EnableExemptionGracePeriod)) {
			SetExemptStatus(ShadowStep, false);
			SetExemptStatus(KnockBack, false);
			SetExemptStatus(Port, false);
		}
		m_time_since_last_position_check     = cur_time;
		m_last_position_check_location       = m_current_position_check_location;
		m_distance_since_last_position_check = 0.0f;
	}
}

void CheatManager::CheckMemTimer()
{
	if (m_target == nullptr) {
		return;
	}
	const uint32 current_time = Timer::GetCurrentTime();
	if (
		m_time_since_last_memorization != 0 &&
		(current_time - m_time_since_last_memorization) <= FastMemorizationWindowMS
	) {
		glm::vec3 pos = m_target->GetPosition();
		CheatDetected(MQFastMem, pos);
	}
	m_time_since_last_memorization = current_time;
}

void CheatManager::ProcessMovementHistory(const EQApplicationPacket *app)
{
	// if they haven't sent sent the packet within this time... they are probably spoofing...
	// linux users reported that they don't send this packet at all but i can't prove they don't so i'm not sure if thats a fake or not.
	m_time_since_last_movement_history.Start(MovementHistoryTimeoutMS);
	if (GetExemptStatus(Port)) {
		return;
	}
	auto *m_MovementHistory = (UpdateMovementEntry *) app->pBuffer;
	if (app->size < sizeof(UpdateMovementEntry)) {
		LogDebug(
			"Size mismatch in OP_MovementHistoryList, expected {}, got [{}]",
			sizeof(UpdateMovementEntry),
			app->size
		);
		DumpPacket(app);
		return;
	}

	for (int index = 0; index < (app->size) / sizeof(UpdateMovementEntry); index++) {
		glm::vec3 to = glm::vec3(m_MovementHistory[index].X, m_MovementHistory[index].Y, m_MovementHistory[index].Z);
		switch (m_MovementHistory[index].type) {
			case UpdateMovementType::ZoneLine:
				SetExemptStatus(Port, true);
				break;
			case UpdateMovementType::TeleportA:
				// Only flag TeleportA if it's a large distance and not during a port exemption
				if (index != 0 && !GetExemptStatus(Port)) {
					glm::vec3 from = glm::vec3(
						m_MovementHistory[index - 1].X,
						m_MovementHistory[index - 1].Y,
						m_MovementHistory[index - 1].Z
					);
					// Only detect as warp if distance is significant (reduces false positives)
					float dist = Distance(from, to);
					if (dist > 100.0f) {  // Reasonable threshold for legitimate vs. illegitimate teleport
						CheatDetected(MQWarpAbsolute, from, to);
					}
				}
				break;
		}
	}
}

void CheatManager::ProcessSpawnApperance(uint16, uint16 type, uint32 parameter)
{
	if (type == AppearanceType::Animation && parameter == Animation::Sitting) {
		m_time_since_last_memorization = Timer::GetCurrentTime();
	}
}

void CheatManager::ClientProcess()
{
	if (!m_cheat_detect_moved) {
		m_time_since_last_position_check = Timer::GetCurrentTime();
	}
}
