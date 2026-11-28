#include "TestScene.h"

#include "OStimBridge.h"

namespace OAA::TestScene
{
	namespace
	{
		std::mutex g_lock;
		std::string g_status;
		std::vector<RE::ActorHandle> g_spawned;

		void SetStatus(std::string a_text)
		{
			logger::info("test scene: {}", a_text);
			std::scoped_lock l(g_lock);
			g_status = std::move(a_text);
		}

		std::string Lower(std::string s)
		{
			std::ranges::transform(s, s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
			return s;
		}

		bool IsCreature(RE::Actor* a_actor)
		{
			const auto race = a_actor ? a_actor->GetRace() : nullptr;
			if (!race) {
				return false;
			}
			const auto edid = Lower(race->GetFormEditorID() ? race->GetFormEditorID() : "");
			return !race->HasKeywordString("ActorTypeNPC") || edid.find("werewolf") != std::string::npos || edid.find("vampirebeast") != std::string::npos;
		}

		RE::TESNPC* FindBase(const char* a_race)
		{
			const auto dh = RE::TESDataHandler::GetSingleton();
			if (!dh) {
				return nullptr;
			}
			const auto want = Lower(a_race);
			RE::TESNPC* fallback = nullptr;
			for (const auto npc : dh->GetFormArray<RE::TESNPC>()) {
				if (!npc || !npc->race || npc->IsDeleted() || !npc->race->GetFormEditorID() || Lower(npc->race->GetFormEditorID()) != want) {
					continue;
				}
				if (npc->IsUnique()) {
					fallback = fallback ? fallback : npc;
					continue;
				}
				return npc;  // a generic one (EncWolf, ...)
			}
			return fallback;
		}

		void Pacify(RE::Actor* a_actor)
		{
			if (auto av = a_actor->AsActorValueOwner()) {
				av->SetActorValue(RE::ActorValue::kAggression, 0.0f);
				av->SetActorValue(RE::ActorValue::kConfidence, 0.0f);
			}
			a_actor->StopCombat();
		}

		RE::Actor* PickPartner(RE::Actor* a_player, std::string& a_how)
		{
			if (auto sel = RE::Console::GetSelectedRef()) {
				if (auto actor = sel->As<RE::Actor>(); actor && actor != a_player && !actor->IsDead()) {
					a_how = "console-selected";
					return actor;
				}
			}
			for (auto it = g_spawned.rbegin(); it != g_spawned.rend(); ++it) {
				if (auto actor = it->get(); actor && !actor->IsDead() && actor->Is3DLoaded()) {
					a_how = "spawned";
					return actor.get();
				}
			}
			RE::Actor* best = nullptr;
			float bestD = 1500.0f;
			if (const auto pl = RE::ProcessLists::GetSingleton()) {
				for (auto& h : pl->highActorHandles) {
					auto actor = h.get();
					if (!actor || actor.get() == a_player || actor->IsDead() || !actor->Is3DLoaded() || !IsCreature(actor.get())) {
						continue;
					}
					const float d = actor->GetPosition().GetDistance(a_player->GetPosition());
					if (d < bestD) {
						bestD = d;
						best = actor.get();
					}
				}
			}
			a_how = "nearest creature";
			return best;
		}

		std::string Describe(RE::Actor* a_actor)
		{
			const auto race = a_actor->GetRace();
			return std::format("{} ({:08X}, {})", a_actor->GetDisplayFullName(), a_actor->GetFormID(),
				race && race->GetFormEditorID() ? race->GetFormEditorID() : "?");
		}
	}

	const std::vector<Choice>& Choices()
	{
		static const std::vector<Choice> list{
			{ "Wolf", "オオカミ", "WolfRace" },
			{ "Dog", "犬", "DogRace" },
			{ "Horse", "馬", "HorseRace" },
			{ "Skeever", "スキーヴァー", "SkeeverRace" },
			{ "Sabre cat", "サーベルキャット", "SabreCatRace" },
			{ "Bear", "クマ", "BearBlackRace" },
			{ "Troll", "トロール", "TrollRace" },
			{ "Draugr", "ドラウグル", "DraugrRace" },
			{ "Falmer", "ファルメル", "FalmerRace" },
			{ "Riekling", "リークリング", "DLC2RieklingRace" },
			{ "Werewolf", "ウェアウルフ", "WerewolfBeastRace" },
		};
		return list;
	}

	void Spawn(int a_index)
	{
		const auto& list = Choices();
		if (a_index < 0 || a_index >= static_cast<int>(list.size())) {
			return;
		}
		const auto race = list[a_index].race;
		SKSE::GetTaskInterface()->AddTask([race]() {
			const auto player = RE::PlayerCharacter::GetSingleton();
			const auto base = FindBase(race);
			if (!player || !base) {
				SetStatus(std::format("no actor base with race {} found", race));
				return;
			}
			auto ref = player->PlaceObjectAtMe(base, false);
			auto actor = ref ? ref->As<RE::Actor>() : nullptr;
			if (!actor) {
				SetStatus(std::format("could not spawn {}", race));
				return;
			}
			// in front of the player, so it doesn't spawn inside them
			const float yaw = player->GetAngleZ();
			auto pos = player->GetPosition();
			pos.x += std::sin(yaw) * 120.0f;
			pos.y += std::cos(yaw) * 120.0f;
			actor->SetPosition(pos, true);
			Pacify(actor);
			g_spawned.push_back(actor->GetHandle());
			SetStatus(std::format("spawned {} from base {:08X}", Describe(actor), base->GetFormID()));
		});
	}

	void Start()
	{
		SKSE::GetTaskInterface()->AddTask([]() {
			const auto player = RE::PlayerCharacter::GetSingleton();
			if (!player) {
				return;
			}
			std::string how;
			auto partner = PickPartner(player, how);
			if (!partner) {
				SetStatus("no partner: spawn a creature, or select an actor in the console first");
				return;
			}
			if (IsCreature(partner)) {
				Pacify(partner);
			}
			// creature first: OCreatures scenes have the creature as actor 0
			const int32_t id = Bridge::StartScene({ partner, player });
			const auto who = std::format("{} ({}{}{})", Describe(partner), how, partner->IsInCombat() ? ", in combat" : "",
				partner->IsHostileToActor(player) ? ", hostile" : "");
			if (id < 0) {
				SetStatus(std::format("OStim refused to start a scene with {}", who));
				return;
			}
			SetStatus(std::format("start requested: thread {} with {}", id, who));
			// OStim can still drop the request later (no fitting scene, actor busy, ...): check back
			std::thread([id, who]() {
				std::this_thread::sleep_for(std::chrono::seconds(6));
				SKSE::GetTaskInterface()->AddTask([id, who]() {
					if (Bridge::IsThreadRunning(id)) {
						SetStatus(std::format("thread {} running with {}", id, who));
					} else {
						SetStatus(std::format("OStim did not start the scene with {} (see OStim.log)", who));
					}
				});
			}).detach();
		});
	}

	void Cleanup()
	{
		SKSE::GetTaskInterface()->AddTask([]() {
			size_t n = 0;
			for (auto& h : g_spawned) {
				if (auto actor = h.get()) {
					actor->Disable();
					actor->SetDelete(true);
					++n;
				}
			}
			g_spawned.clear();
			SetStatus(std::format("removed {} spawned actor(s)", n));
		});
	}

	std::string Status()
	{
		std::scoped_lock l(g_lock);
		return g_status;
	}
}
