#include "Aligner.h"

#include "LegIK.h"

namespace OAA
{
	namespace
	{
		const RE::BSFixedString kNpcNode{ "NPC" };
		const RE::BSFixedString kNpcRootNode{ "NPC Root [Root]" };
		const std::array<RE::BSFixedString, 4> kSupportBones{ RE::BSFixedString{ "NPC L Foot [Lft ]" }, RE::BSFixedString{ "NPC R Foot [Rft ]" },
			RE::BSFixedString{ "NPC L Calf [LClf]" }, RE::BSFixedString{ "NPC R Calf [RClf]" } };
		const RE::BSFixedString kThighL{ "NPC L Thigh [LThg]" };
		const RE::BSFixedString kPelvis{ "NPC Pelvis [Pelv]" };
		const RE::BSFixedString kSpine2{ "NPC Spine2 [Spn2]" };

		bool Near(const RE::NiPoint3& a, const RE::NiPoint3& b, float eps = 0.001f)
		{
			return std::fabs(a.x - b.x) <= eps && std::fabs(a.y - b.y) <= eps && std::fabs(a.z - b.z) <= eps;
		}

		std::string Fmt(const RE::NiPoint3& p)
		{
			return std::format("({:.2f}, {:.2f}, {:.2f})", p.x, p.y, p.z);
		}

		template <class T>
		bool SameActors(const std::vector<RE::ActorHandle>& a_handles, const std::vector<T>& a_states)
		{
			if (a_handles.size() != a_states.size()) {
				return false;
			}
			for (size_t i = 0; i < a_handles.size(); ++i) {
				if (!(a_handles[i] == a_states[i].handle)) {
					return false;
				}
			}
			return true;
		}
	}

	void Aligner::OnNodeChanged(SceneSnapshot a_snapshot)
	{
		const auto settings = Settings::Load();

		std::scoped_lock lock(_lock);

		if (!_anchorsLoaded || !_threads.contains(a_snapshot.threadID)) {
			// re-read anchors.json whenever a new scene starts, so edits apply without a restart
			_anchors = Anchors::Load();
			_anchorsLoaded = true;
		}

		auto& state = _threads[a_snapshot.threadID];
		state.threadID = a_snapshot.threadID;
		state.nodeID = a_snapshot.nodeID;
		state.settings = settings;

		if (!SameActors(a_snapshot.actors, state.actors)) {
			for (auto& old : state.actors) {
				_pendingRestore.push_back(std::move(old));
			}
			state.actors.clear();
			for (auto h : a_snapshot.actors) {
				ActorState s;
				s.handle = h;
				state.actors.push_back(std::move(s));
			}
		}

		state.constraints.clear();
		const auto count = static_cast<int32_t>(state.actors.size());
		std::string summary;
		for (const auto& act : a_snapshot.actions) {
			const auto it = _anchors.actions.find(act.id);
			if (it == _anchors.actions.end()) {
				continue;
			}
			if (act.actor < 0 || act.target < 0 || act.actor >= count || act.target >= count || act.actor == act.target) {
				continue;
			}
			Constraint c;
			c.a = act.actor;
			c.b = act.target;
			c.anchorA = it->second.actorAnchor;
			c.anchorB = it->second.targetAnchor;
			c.weight = it->second.weight;
			c.action = act.id;
			summary += std::format(" {}[{}:{}->{}:{} w{:.1f}]", c.action, c.a, c.anchorA, c.b, c.anchorB, c.weight);
			state.constraints.push_back(std::move(c));
		}

		state.lastOk = -1;
		state.failReason = "not updated yet";
		state.timer = 0.0f;
		state.samples = 0;
		state.sampleLogged = false;
		for (auto& a : state.actors) {
			a.sampleSum = {};
			a.groundedLatch = false;
			a.kneelingLatch = false;
			a.lyingLatch = false;
		}

		logger::info("thread {} -> node '{}' ({} actors, {} actions, {} rules){}{}", state.threadID, state.nodeID, count,
			a_snapshot.actions.size(), state.constraints.size(), summary, settings.enabled ? "" : " [disabled in INI]");
	}

	void Aligner::OnThreadStop(int32_t a_threadID)
	{
		std::scoped_lock lock(_lock);
		if (auto it = _threads.find(a_threadID); it != _threads.end()) {
			for (auto& a : it->second.actors) {
				_pendingRestore.push_back(std::move(a));
			}
			_threads.erase(it);
			logger::info("thread {} stopped, offsets queued for removal", a_threadID);
		}
	}

	void Aligner::RestoreAll()
	{
		std::scoped_lock lock(_lock);
		for (auto& [id, t] : _threads) {
			for (auto& a : t.actors) {
				Restore(a);
			}
		}
		_threads.clear();
		for (auto& a : _pendingRestore) {
			Restore(a);
		}
		_pendingRestore.clear();
	}

	void Aligner::Restore(ActorState& a_actor)
	{
		ClearIK(a_actor.formID);
		if (!a_actor.written || !a_actor.npcNode) {
			return;
		}
		auto& t = a_actor.npcNode->local.translate;
		if (Near(t, a_actor.lastWritten)) {
			t = a_actor.lastWritten - a_actor.appliedLocal;
		}
		// else: somebody (NiOverride, heel mods) rewrote the node since; our offset is already gone
		a_actor.written = false;
		a_actor.appliedLocal = {};
	}

	void Aligner::SetIK(RE::FormID a_id, const RE::NiPoint3& a_offsetLocal, bool a_kneel)
	{
		std::scoped_lock lock(_ikLock);
		auto& e = _ik[a_id];
		if (e.kneel != a_kneel) {
			e.applied = false;
			e.calf.reset();
		}
		e.kneel = a_kneel;
		e.offsetLocal = a_offsetLocal;
		_ikCount = _ik.size();
	}

	void Aligner::ClearIK(RE::FormID a_id)
	{
		if (a_id == 0) {
			return;
		}
		std::scoped_lock lock(_ikLock);
		_ik.erase(a_id);
		_ikCount = _ik.size();
	}

	void Aligner::OnActorAnimated(RE::Actor* a_actor)
	{
		_animCalls.fetch_add(1, std::memory_order_relaxed);
		(void)a_actor;  // diagnostics only; the IK runs from the per-frame update
	}

	RE::NiAVObject* Aligner::FindBone(ActorState& a_actor, const std::string& a_anchor, const Anchors& a_anchors)
	{
		if (auto it = a_actor.boneCache.find(a_anchor); it != a_actor.boneCache.end()) {
			return it->second.get();
		}
		RE::NiAVObject* found = nullptr;
		if (auto bit = a_anchors.bones.find(a_anchor); bit != a_anchors.bones.end() && a_actor.root) {
			for (const auto& name : bit->second) {
				if (auto obj = a_actor.root->GetObjectByName(RE::BSFixedString(name))) {
					found = obj;
					break;
				}
			}
		}
		a_actor.boneCache[a_anchor] = RE::NiPointer<RE::NiAVObject>(found);
		return found;
	}

	void Aligner::Update(float a_delta)
	{
		const float dt = std::clamp(a_delta, 0.0f, 0.1f);

		static bool firstCall = true;
		if (firstCall) {
			firstCall = false;
			logger::info("per-frame update is running (delta {:.4f})", a_delta);
		}
		static uint32_t frames = 0;
		static auto lastBeat = std::chrono::steady_clock::now();
		++frames;

		std::scoped_lock lock(_lock);

		for (auto& a : _pendingRestore) {
			Restore(a);
		}
		_pendingRestore.clear();

		if (const auto now = std::chrono::steady_clock::now(); now - lastBeat >= 5s) {
			if (!_threads.empty()) {
				logger::info("heartbeat: {} frames in the last {:.1f}s, {} active thread(s), {} UpdateAnimation calls", frames,
					std::chrono::duration<float>(now - lastBeat).count(), _threads.size(), _animCalls.exchange(0));
			}
			frames = 0;
			lastBeat = now;
		}

		{
			// did last frame's leg IK survive until now, or did the animation overwrite it?
			std::scoped_lock ikLock(_ikLock);
			for (auto& [fid, e] : _ik) {
				if (e.applied && e.calf) {
					const auto& m = e.calf->local.rotate;
					bool same = true;
					for (int r = 0; r < 3 && same; ++r) {
						for (int c = 0; c < 3; ++c) {
							if (std::fabs(m.entry[r][c] - e.calfLocal.entry[r][c]) > 1e-4f) {
								same = false;
								break;
							}
						}
					}
					(same ? e.persisted : e.overwritten)++;
				}
			}
		}

		for (auto& [id, state] : _threads) {
			const int ok = UpdateThread(state, dt) ? 1 : 0;
			if (ok != state.lastOk) {
				state.lastOk = ok;
				if (!ok) {
					logger::warn("thread {} node '{}': skipped ({})", state.threadID, state.nodeID, state.failReason);
				} else {
					logger::info("thread {} node '{}': updating", state.threadID, state.nodeID);
				}
			}
		}
	}

	bool Aligner::UpdateThread(ThreadState& a_state, float a_delta)
	{
		const auto& cfg = a_state.settings;
		const auto n = a_state.actors.size();
		if (n == 0) {
			a_state.failReason = "no actors";
			return false;
		}

		// --- 1. resolve actors and skeleton nodes ---
		float hMin = 1e9f, hMax = -1e9f;
		for (size_t ai = 0; ai < n; ++ai) {
			auto& a = a_state.actors[ai];
			const auto actor = a.handle.get();
			if (!actor) {
				a_state.failReason = std::format("actor {} handle invalid", ai);
				return false;
			}
			const auto root = actor->Get3D(false);
			if (!root) {
				a_state.failReason = std::format("actor {} has no 3D", ai);
				return false;
			}
			const auto npc = root->GetObjectByName(kNpcNode);
			if (!npc || !npc->parent) {
				a_state.failReason = std::format("actor {} has no 'NPC' node (root '{}')", ai, root->name.c_str());
				return false;
			}
			if (npc != a.npcNode.get()) {
				// 3D was (re)loaded: start from scratch on the new skeleton
				a.npcNode.reset(npc);
				a.boneCache.clear();
				a.written = false;
				a.appliedLocal = {};
				a.currentWorld = {};
			}
			a.root = root;
			a.parent = npc->parent;
			a.isPlayer = actor->IsPlayerRef();
			a.formID = actor->GetFormID();
			const auto npcRoot = root->GetObjectByName(kNpcRootNode);
			a.height = npcRoot ? npcRoot->world.scale : npc->world.scale;
			if (a.height <= 0.01f) {
				a.height = 1.0f;
			}
			// world-space effect of what we wrote last frame (the bones we read were posed with it)
			a.prevWorld = (a.parent->world.rotate * a.appliedLocal) * a.parent->world.scale;

			// standing/kneeling/lying on the floor = some lower-body bone is near the actor's base;
			// such an actor can't move vertically without sinking into or floating above the floor
			// if the leg IK kept the feet (and, when kneeling, the knees) planted, those bones are
			// already at their animated height
			bool planted = false, plantedKnees = false;
			{
				std::scoped_lock ikLock(_ikLock);
				if (auto it = _ik.find(a.formID); it != _ik.end()) {
					planted = it->second.applied;
					plantedKnees = planted && it->second.kneel;
				}
			}
			const float baseZ = root->world.translate.z;
			a.supportHeight = 1e9f;
			float kneeZ = 1e9f;
			for (size_t bi = 0; bi < kSupportBones.size(); ++bi) {
				if (auto bone = root->GetObjectByName(kSupportBones[bi])) {
					const bool isFoot = bi < 2;
					const float drop = ((planted && isFoot) || (plantedKnees && !isFoot)) ? 0.0f : a.prevWorld.z;
					const float z = bone->world.translate.z - drop - baseZ;
					a.supportHeight = std::min(a.supportHeight, z);
					if (!isFoot) {
						kneeZ = std::min(kneeZ, z);
					}
				}
			}
			// Right after a scene change the bones can still hold the previous stage's pose (e.g. both
			// standing before a lift), so only start latching "grounded" once the new pose has settled.
			const bool settled = a_state.timer >= cfg.settleSeconds;
			a.grounded = (settled && a.groundedLatch) || a.supportHeight < cfg.groundedHeight * a.height;
			if (settled) {
				a.groundedLatch = a.grounded;
			}

			// lying / sitting on the floor: pelvis or chest near the floor. Such an actor can neither
			// bend down nor rise, whatever the legs do (legs up on a partner's shoulders, etc.)
			float pelvisZ = 1e9f, chestZ = 1e9f;
			if (const auto b = root->GetObjectByName(kPelvis)) {
				pelvisZ = b->world.translate.z - a.prevWorld.z - baseZ;
			}
			if (const auto b = root->GetObjectByName(kSpine2)) {
				chestZ = b->world.translate.z - a.prevWorld.z - baseZ;
			}
			const bool lyingNow = std::min(pelvisZ, chestZ) < cfg.lyingHeight * a.height;
			a.lying = (settled && a.lyingLatch) || lyingNow;
			if (settled) {
				a.lyingLatch = a.lying;
			}
			if (a.lying) {
				a.grounded = true;
				if (settled) {
					a.groundedLatch = true;
				}
			}
			const bool torsoUpright = chestZ - pelvisZ > cfg.kneelTorsoUp * a.height;

			// kneeling / sitting on heels: knees on the floor, hips clearly above them, torso upright
			// (not lying, not on all fours)
			a.kneelRiseMax = 0.0f;
			bool kneelNow = false;
			if (const auto thighL = root->GetObjectByName(kThighL)) {
				const float hipZ = thighL->world.translate.z - a.prevWorld.z - baseZ;
				kneelNow = a.grounded && !a.lying && torsoUpright && kneeZ < cfg.kneelHeight * a.height && hipZ > cfg.kneelMinHipHeight * a.height;
				if (const auto calfL = root->GetObjectByName(kSupportBones[2])) {
					const float thighLen = (calfL->world.translate - thighL->world.translate).Length();
					a.kneelRiseMax = std::clamp(0.8f * thighLen - (hipZ - kneeZ), 0.0f, cfg.maxKneelRise * a.height);
				}
			}
			a.kneeling = a.grounded && !a.lying && ((settled && a.kneelingLatch) || kneelNow);
			if (settled) {
				a.kneelingLatch = a.kneeling;
			}
			hMin = std::min(hMin, a.height);
			hMax = std::max(hMax, a.height);
		}

		// --- 2. per-rule relation that would hold at the animation's reference height (1.0) ---
		// A bone at P with skeleton scale h sits at T + R*(h*l). At h=1 it would be at T + R*l,
		// so the error caused by the height is (P - T) * (1 - 1/h). We want, for every rule,
		// (offset_a - offset_b) = -(err_a - err_b).
		struct Rel
		{
			int32_t a, b;
			float w;
			RE::NiPoint3 r;
		};
		std::vector<Rel> rels;
		rels.reserve(a_state.constraints.size());
		for (const auto& c : a_state.constraints) {
			auto& A = a_state.actors[c.a];
			auto& B = a_state.actors[c.b];
			const auto boneA = FindBone(A, c.anchorA, _anchors);
			const auto boneB = FindBone(B, c.anchorB, _anchors);
			if (!boneA || !boneB) {
				continue;
			}
			auto err = [&](const ActorState& s, RE::NiAVObject* bone) {
				if (std::fabs(s.height - 1.0f) < cfg.minHeightDiff) {
					return RE::NiPoint3{};
				}
				const auto p0 = bone->world.translate - s.prevWorld;
				return (p0 - s.root->world.translate) * (1.0f - 1.0f / s.height);
			};
			rels.push_back({ c.a, c.b, c.weight, -(err(A, boneA) - err(B, boneB)) });
		}

		// --- 3. weighted least squares for one offset per actor (Gauss-Seidel, per axis) ---
		// mobility 0 = pinned on that axis; horizontal and vertical are handled separately so a
		// grounded actor can still slide on the floor.
		std::vector<float> mobH(n, 1.0f), mobBase(n, 1.0f), mobZ(n, 1.0f);
		constexpr float kPinned = 0.02f;
		for (size_t i = 0; i < n; ++i) {
			const auto& a = a_state.actors[i];
			float m = 1.0f;
			switch (cfg.moveMode) {
			case MoveMode::kShorter:
				m = (hMax - hMin > 0.001f && a.height >= hMax - 0.001f) ? kPinned : 1.0f;
				break;
			case MoveMode::kTaller:
				m = (hMax - hMin > 0.001f && a.height <= hMin + 0.001f) ? kPinned : 1.0f;
				break;
			case MoveMode::kNPCsOnly:
				m = (a.isPlayer && n > 1) ? kPinned : 1.0f;
				break;
			default:
				break;
			}
			mobH[i] = m;
			mobBase[i] = m;
		}

		std::vector<RE::NiPoint3> solved(n);
		std::vector<float> zFix(n, std::numeric_limits<float>::quiet_NaN());
		auto solve = [&]() {
			std::ranges::fill(solved, RE::NiPoint3{});
			for (size_t i = 0; i < n; ++i) {
				if (!std::isnan(zFix[i])) {
					solved[i].z = zFix[i];
				}
			}
			float wSum = 0.0f;
			for (auto& r : rels) {
				wSum += r.w;
			}
			const float lambda = 0.01f * wSum;
			for (int iter = 0; iter < 60; ++iter) {
				for (size_t i = 0; i < n; ++i) {
					RE::NiPoint3 num{};
					float w = 0.0f;
					for (auto& r : rels) {
						if (r.a == static_cast<int32_t>(i)) {
							num += (solved[r.b] + r.r) * r.w;
							w += r.w;
						} else if (r.b == static_cast<int32_t>(i)) {
							num += (solved[r.a] - r.r) * r.w;
							w += r.w;
						}
					}
					// with a tiny regulariser the split between actors follows the mobility ratio
					auto axis = [&](float a_num, float a_mob) {
						return a_mob <= 0.0001f ? 0.0f : a_num / (lambda / a_mob + w);
					};
					const float z = std::isnan(zFix[i]) ? axis(num.z, mobZ[i]) : zFix[i];
					solved[i] = { axis(num.x, mobH[i]), axis(num.y, mobH[i]), z };
				}
			}
		};

		if (!rels.empty()) {
			// Vertical rules with leg IK:
			//  - lifted actors move freely;
			//  - a standing actor may go DOWN (knees bend, feet planted), never up;
			//  - a kneeling actor may go UP (thighs swing up, knees planted) up to kneelRiseMax, never down;
			//  - a grounded actor tied to a lifted one keeps its height (the lifted one takes it all).
			// Where both a standing and a kneeling actor could move, fKneelRiseShare decides who does more.
			const bool kneelLift = cfg.legIK && cfg.kneelLift;
			auto isKneeler = [&](size_t i) { return kneelLift && a_state.actors[i].kneeling; };
			for (size_t i = 0; i < n; ++i) {
				if (a_state.actors[i].lying) {
					zFix[i] = 0.0f;
				}
			}
			for (auto& r : rels) {
				const bool ga = a_state.actors[r.a].grounded, gb = a_state.actors[r.b].grounded;
				if (cfg.legIK && ga && !gb) {
					zFix[r.a] = 0.0f;
				} else if (cfg.legIK && gb && !ga) {
					zFix[r.b] = 0.0f;
				}
			}
			for (size_t i = 0; i < n; ++i) {
				const auto& a = a_state.actors[i];
				if (!a.grounded) {
					mobZ[i] = mobBase[i];
				} else if (!cfg.legIK) {
					mobZ[i] = cfg.groundedZMobility * mobBase[i];
				} else if (isKneeler(i)) {
					mobZ[i] = cfg.kneelRiseShare * mobBase[i];
				} else {
					mobZ[i] = (1.0f - cfg.kneelRiseShare) * mobBase[i];
				}
			}
			for (size_t pass = 0; pass <= 2 * n; ++pass) {
				solve();
				if (!cfg.legIK) {
					break;
				}
				bool changed = false;
				for (size_t i = 0; i < n; ++i) {
					const auto& a = a_state.actors[i];
					if (!a.grounded || !std::isnan(zFix[i])) {
						continue;
					}
					const float z = solved[i].z;
					float fix = std::numeric_limits<float>::quiet_NaN();
					if (isKneeler(i)) {
						if (z < -0.01f) {
							fix = 0.0f;
						} else if (z > a.kneelRiseMax) {
							fix = a.kneelRiseMax;
						}
					} else if (z > 0.01f) {
						fix = 0.0f;
					}
					if (!std::isnan(fix)) {
						zFix[i] = fix;
						changed = true;
					}
				}
				if (!changed) {
					break;
				}
			}
			if (cfg.legIK) {
				for (size_t i = 0; i < n; ++i) {
					if (a_state.actors[i].grounded && !isKneeler(i)) {
						solved[i].z = std::max(solved[i].z, -cfg.maxLegDrop * a_state.actors[i].height);
					}
				}
			}
		}

		// --- 4. choose the target (realtime or stage average) ---
		a_state.timer += a_delta;
		const bool sampling = cfg.mode == Mode::kStage && a_state.timer >= cfg.settleSeconds &&
		                      a_state.timer < cfg.settleSeconds + cfg.sampleSeconds;
		if (sampling) {
			for (size_t i = 0; i < n; ++i) {
				a_state.actors[i].sampleSum += solved[i];
			}
			++a_state.samples;
		}

		const float alpha = cfg.smoothSeconds <= 0.0f ? 1.0f : 1.0f - std::exp(-a_delta / cfg.smoothSeconds);

		for (size_t i = 0; i < n; ++i) {
			auto& a = a_state.actors[i];

			RE::NiPoint3 target{};
			if (cfg.enabled) {
				if (cfg.mode == Mode::kRealtime) {
					target = solved[i];
				} else if (a_state.samples > 0) {
					target = a.sampleSum / static_cast<float>(a_state.samples);
				} else {
					target = a.currentWorld;  // keep the previous stage's offset until we have samples
				}
				if (cfg.mode == Mode::kRealtime || a_state.samples > 0) {
					target.x *= cfg.horizontalScale * cfg.strength;
					target.y *= cfg.horizontalScale * cfg.strength;
					target.z *= cfg.verticalScale * cfg.strength;
					const float len = target.Length();
					if (cfg.maxOffset > 0.0f && len > cfg.maxOffset) {
						target *= cfg.maxOffset / len;
					}
				}
			}

			a.currentWorld += (target - a.currentWorld) * alpha;

			// --- 5. write into the "NPC" node (local space of its parent) ---
			const float ps = a.parent->world.scale > 0.0001f ? a.parent->world.scale : 1.0f;
			const auto local = (a.parent->world.rotate.Transpose() * a.currentWorld) / ps;

			auto& t = a.npcNode->local.translate;
			const RE::NiPoint3 base = (a.written && Near(t, a.lastWritten)) ? a.lastWritten - a.appliedLocal : t;
			t = base + local;
			a.lastWritten = t;
			a.appliedLocal = local;
			a.written = true;

			const bool kneelIK = cfg.kneelLift && a.kneeling && a.currentWorld.z > 0.3f;
			const bool standIK = !a.kneeling && !a.lying && a.currentWorld.z < -0.3f;
			if (cfg.enabled && cfg.legIK && a.grounded && (kneelIK || standIK)) {
				SetIK(a.formID, local, kneelIK);
				// apply right here too: this runs after the animation pose was written and before
				// rendering (the NPC translation written here is visible), so bending now shows up
				// if the animation didn't re-pose the legs since last frame (paused anim), our bend is
				// still there and must not be applied on top of itself
				bool stillBent = false;
				{
					std::scoped_lock ikLock(_ikLock);
					if (auto it = _ik.find(a.formID); it != _ik.end() && it->second.applied && it->second.calf) {
						const auto& m = it->second.calf->local.rotate;
						stillBent = true;
						for (int r = 0; r < 3 && stillBent; ++r) {
							for (int c = 0; c < 3; ++c) {
								if (std::fabs(m.entry[r][c] - it->second.calfLocal.entry[r][c]) > 1e-5f) {
									stillBent = false;
									break;
								}
							}
						}
					}
				}
				RE::NiUpdateData ud{};
				a.npcNode->Update(ud);
				if (stillBent) {
					continue;
				}
				std::string failInfo;
				const auto r = LegIK::Apply(a.root, local, kneelIK ? LegIK::Mode::kKneel : LegIK::Mode::kStand, a.ikFailLogged ? nullptr : &failInfo);
				std::scoped_lock ikLock(_ikLock);
				if (auto it = _ik.find(a.formID); it != _ik.end()) {
					it->second.applied = r.applied;
					it->second.footError = r.footError;
					it->second.calf.reset(r.calf);
					it->second.calfLocal = r.calfLocal;
				}
				if (r.applied && !_ikLogged) {
					_ikLogged = true;
					logger::info("leg IK applied for the first time (actor {:08X}, foot error {:.2f})", a.formID, r.footError);
				}
				if (!r.applied && !a.ikFailLogged) {
					a.ikFailLogged = true;
					logger::warn("leg IK could not use the leg bones of actor {:08X}: {}", a.formID, failInfo.empty() ? "too short / degenerate"s : failInfo);
				}
			} else {
				ClearIK(a.formID);
			}
		}

		if (cfg.mode == Mode::kStage && !a_state.sampleLogged && a_state.timer >= cfg.settleSeconds + cfg.sampleSeconds) {
			a_state.sampleLogged = true;
			std::string s;
			for (size_t i = 0; i < n; ++i) {
				const auto& a = a_state.actors[i];
				const auto avg = a_state.samples ? a.sampleSum / static_cast<float>(a_state.samples) : RE::NiPoint3{};
				s += std::format(" [{}] h={:.3f}{} offset={}", i, a.height, a.lying ? " lying" : a.grounded ? (a.kneeling ? " kneeling" : " grounded") : " lifted", Fmt(avg));
			}
			logger::info("thread {} node '{}' settled ({} samples, {} rules):{}", a_state.threadID, a_state.nodeID, a_state.samples, rels.size(), s);
		}

		if (cfg.verbose) {
			a_state.verboseTimer += a_delta;
			if (a_state.verboseTimer >= 1.0f) {
				a_state.verboseTimer = 0.0f;
				std::string s;
				for (size_t i = 0; i < n; ++i) {
					const auto& a = a_state.actors[i];
					s += std::format(" [{}] h={:.3f} support={:.1f}{} solved={} applied={}", i, a.height, a.supportHeight, a.lying ? " lying"s : a.grounded ? (a.kneeling ? std::format(" kneeling(rise<={:.1f})", a.kneelRiseMax) : " grounded"s) : " lifted"s, Fmt(solved[i]), Fmt(a.currentWorld));
					std::scoped_lock ikLock(_ikLock);
					if (auto it = _ik.find(a.formID); it != _ik.end()) {
						s += std::format(" IK(applied={} footErr={:.2f} survived={}/{})", it->second.applied, it->second.footError,
							it->second.persisted, it->second.persisted + it->second.overwritten);
					}
				}
				logger::info("thread {} t={:.1f}s rules={}:{}", a_state.threadID, a_state.timer, rels.size(), s);
			}
		}
		return true;
	}
}
