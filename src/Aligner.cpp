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

		// heel height of a shoe mesh, read the same way OStim / the heel frameworks do
		float MeshHeel(RE::NiAVObject* a_obj)
		{
			if (!a_obj) {
				return 0.0f;
			}
			if (auto hh = a_obj->GetExtraData<RE::NiFloatExtraData>("HH_OFFSET")) {
				return hh->value;
			}
			if (auto sdta = a_obj->GetExtraData<RE::NiStringExtraData>("SDTA"); sdta && sdta->value) {
				const auto j = nlohmann::json::parse(sdta->value, nullptr, false);
				if (j.is_array()) {
					for (const auto& e : j) {
						if (e.is_object() && e.value("name", "") == "NPC" && e.contains("pos") && e["pos"].is_array() && e["pos"].size() >= 3) {
							return e["pos"][2].get<float>();
						}
					}
				}
			}
			return 0.0f;
		}

		float ShoeHeel(RE::Actor* a_actor)
		{
			auto& biped = a_actor->GetBiped(false);
			if (!biped) {
				return 0.0f;
			}
			for (auto& data : biped->objects) {
				if (!data.partClone || !data.item || data.item->GetFormType() != RE::FormType::Armor || !data.addon ||
					!data.addon->HasPartOf(RE::BIPED_MODEL::BipedObjectSlot::kFeet)) {
					continue;
				}
				auto obj = data.partClone.get();
				if (float h = MeshHeel(obj); h != 0.0f) {
					return h;
				}
				if (auto node = obj->AsNode()) {
					for (auto& child : node->GetChildren()) {
						if (float h = MeshHeel(child.get()); h != 0.0f) {
							return h;
						}
					}
				}
			}
			return 0.0f;
		}

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

		state.handRules.clear();
		std::string handSummary;
		for (const auto& act : a_snapshot.actions) {
			const auto it = _anchors.handActions.find(act.id);
			if (it == _anchors.handActions.end()) {
				continue;
			}
			if (act.actor < 0 || act.target < 0 || act.actor >= count || act.target >= count || act.actor == act.target) {
				continue;
			}
			state.handRules.push_back({ act.actor, act.target, it->second, act.id });
			handSummary += std::format(" {}[{}:hand->{}:{}]", act.id, act.actor, act.target, it->second);
		}
		if (!handSummary.empty()) {
			logger::info("thread {} node '{}' hand rules:{}", state.threadID, state.nodeID, handSummary);
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

	void Aligner::ApplySettings(const Settings& a_settings)
	{
		std::scoped_lock lock(_lock);
		for (auto& [id, t] : _threads) {
			t.settings = a_settings;
		}
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

	void Aligner::ReleaseArm(ArmState& a_arm)
	{
		if (a_arm.written && a_arm.upper && a_arm.lower && a_arm.end) {
			auto same = [](const RE::NiMatrix3& x, const RE::NiMatrix3& y) {
				for (int r = 0; r < 3; ++r) {
					for (int c = 0; c < 3; ++c) {
						if (std::fabs(x.entry[r][c] - y.entry[r][c]) > 1e-5f) {
							return false;
						}
					}
				}
				return true;
			};
			// only undo if the animation hasn't re-posed the arm since (paused animation)
			if (same(a_arm.upper->local.rotate, a_arm.wroteU) && same(a_arm.lower->local.rotate, a_arm.wroteL) &&
				same(a_arm.end->local.rotate, a_arm.wroteE)) {
				a_arm.upper->local.rotate = a_arm.origU;
				a_arm.lower->local.rotate = a_arm.origL;
				a_arm.end->local.rotate = a_arm.origE;
				RE::NiUpdateData ud{};
				a_arm.upper->Update(ud);
			}
		}
		a_arm.written = false;
	}

	void Aligner::ReleaseSpine(SpineState& a_spine)
	{
		if (a_spine.written) {
			bool same = true;
			for (size_t b = 0; b < 2 && same; ++b) {
				if (!a_spine.bones[b]) {
					continue;
				}
				const auto& m = a_spine.bones[b]->local.rotate;
				for (int r = 0; r < 3 && same; ++r) {
					for (int c = 0; c < 3; ++c) {
						if (std::fabs(m.entry[r][c] - a_spine.wrote[b].entry[r][c]) > 1e-5f) {
							same = false;
							break;
						}
					}
				}
			}
			if (same) {
				for (size_t b = 0; b < 2; ++b) {
					if (a_spine.bones[b]) {
						a_spine.bones[b]->local.rotate = a_spine.orig[b];
					}
				}
				if (a_spine.bones[0]) {
					RE::NiUpdateData ud{};
					a_spine.bones[0]->Update(ud);
				}
			}
		}
		a_spine.written = false;
		a_spine.angle = 0.0f;
	}

	const std::vector<RE::NiPointer<RE::NiAVObject>>& Aligner::HandTargets(ActorState& a_actor, const std::string& a_name)
	{
		if (auto it = a_actor.targetCache.find(a_name); it != a_actor.targetCache.end()) {
			return it->second;
		}
		std::vector<RE::NiPointer<RE::NiAVObject>> found;
		auto scan = [&](const std::unordered_map<std::string, std::vector<std::vector<std::string>>>& a_map) {
			if (auto it = a_map.find(a_name); it != a_map.end()) {
				for (const auto& group : it->second) {
					for (const auto& name : group) {
						if (auto obj = a_actor.root->GetObjectByName(RE::BSFixedString(name))) {
							found.emplace_back(obj);
						}
					}
					if (!found.empty()) {
						break;
					}
				}
			}
		};
		if (a_actor.root) {
			if (a_actor.creature) {
				scan(_anchors.creatureHandTargets);
			}
			if (found.empty()) {
				scan(_anchors.handTargets);
			}
		}
		return a_actor.targetCache[a_name] = std::move(found);
	}

	void Aligner::Restore(ActorState& a_actor)
	{
		ClearIK(a_actor.formID);
		for (auto& arm : a_actor.arms) {
			ReleaseArm(arm);
			arm = ArmState{};
		}
		ReleaseSpine(a_actor.spine);
		a_actor.spine = SpineState{};
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

	void Aligner::UpdateArms(ThreadState& a_state, float a_delta)
	{
		static const std::array<std::array<RE::BSFixedString, 3>, 2> kArms{ std::array<RE::BSFixedString, 3>{ "NPC L UpperArm [LUar]", "NPC L Forearm [LLar]", "NPC L Hand [LHnd]" },
			std::array<RE::BSFixedString, 3>{ "NPC R UpperArm [RUar]", "NPC R Forearm [RLar]", "NPC R Hand [RHnd]" } };

		static const std::array<RE::BSFixedString, 2> kFingers{ RE::BSFixedString{ "NPC L Finger20 [LF20]" }, RE::BSFixedString{ "NPC R Finger20 [RF20]" } };
		// contact point = middle of the palm (between the wrist and the middle-finger knuckle); the
		// wrist bone itself sits behind the palm, so aiming it would put the fingers past the target
		auto palm = [](const ArmState& a_arm) {
			return a_arm.finger ? (a_arm.end->world.translate + a_arm.finger->world.translate) * 0.5f : a_arm.end->world.translate;
		};

		const auto& cfg = a_state.settings;
		const bool active = cfg.enabled && cfg.armIK && !IsPaused();
		const size_t n = a_state.actors.size();

		// 1. undo last frame's edit where the animation didn't overwrite it, and remember the pose
		static const std::array<RE::BSFixedString, 2> kSpine{ RE::BSFixedString{ "NPC Spine1 [Spn1]" }, RE::BSFixedString{ "NPC Spine2 [Spn2]" } };
		for (auto& a : a_state.actors) {
			if (a.creature) {
				continue;  // creature limbs are never bent
			}
			if (!a.spine.resolved) {
				a.spine.resolved = true;
				for (size_t b = 0; b < 2; ++b) {
					if (auto n = a.root->GetObjectByName(kSpine[b]); n && n->parent) {
						a.spine.bones[b].reset(n);
					}
				}
			}
			ReleaseSpine(a.spine);
			for (size_t b = 0; b < 2; ++b) {
				if (a.spine.bones[b]) {
					a.spine.orig[b] = a.spine.bones[b]->local.rotate;
				}
			}
			for (size_t k = 0; k < 2; ++k) {
				auto& arm = a.arms[k];
				if (!arm.resolved) {
					arm.resolved = true;
					auto u = a.root->GetObjectByName(kArms[k][0]);
					auto l = a.root->GetObjectByName(kArms[k][1]);
					auto e = a.root->GetObjectByName(kArms[k][2]);
					if (u && l && e && u->parent && l->parent && e->parent && LegIK::IsAncestorOf(u, l) && LegIK::IsAncestorOf(l, e)) {
						arm.upper.reset(u);
						arm.lower.reset(l);
						arm.end.reset(e);
						if (auto f = a.root->GetObjectByName(kFingers[k]); f && LegIK::IsAncestorOf(e, f)) {
							arm.finger.reset(f);
						}
					}
				}
				if (!arm.upper) {
					continue;
				}
				ReleaseArm(arm);
				arm.origU = arm.upper->local.rotate;
				arm.origL = arm.lower->local.rotate;
				arm.origE = arm.end->local.rotate;
			}
		}

		// world position the bone would have if its actor had reference height (and no offset of ours)
		auto ref = [](const ActorState& s, const RE::NiPoint3& p) {
			const auto rootP = s.root->world.translate;
			return rootP + (p - s.currentWorld - rootP - s.heelWorld) / s.height;
		};

		// 2. per hand: nearest target among this stage's hand rules (in reference space)
		struct Job
		{
			ArmState* arm;
			size_t owner;
			RE::NiPoint3 handPos, desired, pole;
			float weight;
			const std::string* target;
		};
		std::vector<Job> jobs;
		const float alpha = cfg.handSmoothSeconds <= 0.0f ? 1.0f : 1.0f - std::exp(-a_delta / cfg.handSmoothSeconds);
		for (size_t i = 0; i < n; ++i) {
			auto& A = a_state.actors[i];
			if (A.creature) {
				continue;
			}
			const auto down = A.root->world.rotate * RE::NiPoint3{ 0.0f, 0.0f, -1.0f };
			for (size_t k = 0; k < 2; ++k) {
				auto& arm = A.arms[k];
				if (!arm.upper) {
					continue;
				}
				const auto handPos = palm(arm);
				const auto handRef = ref(A, handPos);
				float best = 1e9f;
				RE::NiPoint3 desired = handPos;
				const std::string* target = nullptr;
				if (active) {
					for (const auto& hr : a_state.handRules) {
						if (hr.a != static_cast<int32_t>(i)) {
							continue;
						}
						auto& Bt = a_state.actors[hr.b];
						for (const auto& bone : HandTargets(Bt, hr.target)) {
							const auto tp = bone->world.translate;
							const auto gap = handRef - ref(Bt, tp);
							const float d = gap.Length();
							if (d < best) {
								best = d;
								// the gap was measured on a reference-size body part; on the real one it
								// scales with the target's size (a smaller head = hand closer to the bone)
								desired = tp + gap * Bt.height;
								if (hr.target == "breast" && (cfg.breastUp != 0.0f || cfg.breastForward != 0.0f)) {
									// body presets with bigger/heavier breasts than the animation's
									// reference body put the surface somewhere else; user offset
									desired += (Bt.root->world.rotate * RE::NiPoint3{ 0.0f, cfg.breastForward, cfg.breastUp }) * Bt.height;
								}
								target = &hr.target;
								arm.gap = gap;
								arm.targetBone = bone;
							}
						}
					}
				}
				const float reach = cfg.handReach * A.height;
				const float wanted = target ? std::clamp((1.5f * reach - best) / (0.5f * reach), 0.0f, 1.0f) : 0.0f;
				auto raw = desired - handPos;
				const float maxShift = cfg.maxHandShift * A.height;
				if (const float len = raw.Length(); len > maxShift && len > 0.0f) {
					raw *= maxShift / len;
				}
				if (wanted > 0.0f) {
					arm.delta += (raw - arm.delta) * alpha;
				}
				arm.weight += (wanted - arm.weight) * alpha;
				if (target) {
					arm.target = *target;
				}
				if (arm.weight > 0.01f) {
					jobs.push_back({ &arm, i, handPos, handPos + arm.delta * arm.weight, down, arm.weight, &arm.target });
				} else {
					arm.weight = 0.0f;
					arm.delta = {};
				}
			}
		}

		// 3. lean: a person reaching down/forward bends the upper body instead of breaking the wrist.
		// Swing Spine1 then Spine2 so the reaching shoulders travel part of the hand correction.
		if (active && cfg.spineLean > 0.0f) {
			for (size_t i = 0; i < n; ++i) {
				auto& A = a_state.actors[i];
				if (!A.spine.bones[0] || !A.spine.bones[1] || A.lying) {
					continue;
				}
				RE::NiPoint3 shoulder{}, move{};
				float w = 0.0f;
				for (auto& j : jobs) {
					if (j.owner != i) {
						continue;
					}
					shoulder += j.arm->upper->world.translate * j.weight;
					move += (j.desired - j.handPos) * j.weight;
					w += j.weight;
				}
				if (w <= 0.0f) {
					continue;
				}
				shoulder = shoulder / w;
				move = move / w;
				if (move.Length() < 2.0f) {
					continue;
				}
				const auto goal = shoulder + move * cfg.spineLean;
				const float maxA = cfg.maxSpineLean * 0.0174533f;
				float total = LegIK::SwingToward(A.spine.bones[0].get(), shoulder, goal, 0.5f, maxA * 0.5f);
				// shoulders moved with Spine1; recompute where they are now
				RE::NiPoint3 shoulder2{};
				for (auto& j : jobs) {
					if (j.owner == i) {
						shoulder2 += j.arm->upper->world.translate * j.weight;
					}
				}
				shoulder2 = shoulder2 / w;
				total += LegIK::SwingToward(A.spine.bones[1].get(), shoulder2, goal, 1.0f, maxA - total);
				for (size_t b = 0; b < 2; ++b) {
					A.spine.wrote[b] = A.spine.bones[b]->local.rotate;
				}
				A.spine.written = true;
				A.spine.angle = total;
			}
		}

		// 4. apply (after all targets were read, so hand-to-hand rules see the animated pose)
		for (auto& j : jobs) {
			auto& arm = *j.arm;
			j.handPos = palm(arm);  // the palm moved if the spine leaned
			// The hand keeps (mostly) its animated world orientation, so it meets the body part at the
			// angle the animator intended. To avoid a broken wrist, pick the elbow swivel that needs the
			// least wrist bend for that orientation; then iterate so the palm lands on the target.
			const auto oU = arm.upper->local.rotate, oL = arm.lower->local.rotate, oE = arm.end->local.rotate;
			auto resetPose = [&]() {
				arm.upper->local.rotate = oU;
				arm.lower->local.rotate = oL;
				arm.end->local.rotate = oE;
				RE::NiUpdateData ud{};
				arm.upper->Update(ud);
			};
			const float maxWrist = cfg.maxWristAngle * 0.0174533f;
			const auto wristStart = arm.end->world.translate + (j.desired - j.handPos);
			float bestSwivel = 0.0f, bestBend = 1e9f;
			const float range = cfg.elbowSwivel * 0.0174533f;
			constexpr int kSteps = 4;
			for (int si = -kSteps; si <= kSteps; ++si) {
				const float sw = range * static_cast<float>(si) / kSteps;
				float bend = 0.0f;
				LegIK::Reach(arm.upper.get(), arm.lower.get(), arm.end.get(), wristStart, j.pole, cfg.wristFollow, maxWrist, sw, &bend);
				bend += 0.15f * std::fabs(sw);  // prefer the animated elbow when it's about equal
				if (bend < bestBend) {
					bestBend = bend;
					bestSwivel = sw;
				}
				resetPose();
			}
			auto wristTarget = wristStart;
			for (int it = 0; it < 3; ++it) {
				if (it > 0) {
					resetPose();
				}
				LegIK::Reach(arm.upper.get(), arm.lower.get(), arm.end.get(), wristTarget, j.pole, cfg.wristFollow, maxWrist, bestSwivel, &arm.wristBend);
				const auto miss = j.desired - palm(arm);
				arm.lastError = miss.Length();
				if (arm.lastError < 0.3f) {
					break;
				}
				wristTarget += miss;
			}
			arm.swivel = bestSwivel;
			arm.wroteU = arm.upper->local.rotate;
			arm.wroteL = arm.lower->local.rotate;
			arm.wroteE = arm.end->local.rotate;
			arm.written = true;
			if (!_armLogged) {
				_armLogged = true;
				logger::info("arm IK applied for the first time (target '{}', shift {:.1f}, remaining error {:.2f})", *j.target,
					(j.desired - j.handPos).Length(), arm.lastError);
			}
		}
	}

	RE::NiAVObject* Aligner::FindBone(ActorState& a_actor, const std::string& a_anchor, const Anchors& a_anchors)
	{
		if (auto it = a_actor.boneCache.find(a_anchor); it != a_actor.boneCache.end()) {
			return it->second.get();
		}
		RE::NiAVObject* found = nullptr;
		auto scan = [&](const std::unordered_map<std::string, std::vector<std::string>>& a_map, const std::string& a_key) {
			if (found || !a_actor.root) {
				return;
			}
			if (auto bit = a_map.find(a_key); bit != a_map.end()) {
				for (const auto& name : bit->second) {
					if (auto obj = a_actor.root->GetObjectByName(RE::BSFixedString(name))) {
						found = obj;
						break;
					}
				}
			}
		};
		if (a_actor.creature) {
			scan(a_anchors.creatureBones, a_anchor);
		}
		scan(a_anchors.bones, a_anchor);
		if (a_actor.creature) {
			// a creature is the fixed reference of its scene: any bone near the part will do
			scan(a_anchors.creatureBones, "fallback"s);
		}
		a_actor.boneCache[a_anchor] = RE::NiPointer<RE::NiAVObject>(found);
		return found;
	}

	bool Aligner::IsCreature(RE::Actor* a_actor, const Anchors& a_anchors, std::string& a_race)
	{
		const auto race = a_actor->GetRace();
		a_race = race && race->GetFormEditorID() ? race->GetFormEditorID() : "";
		std::string lower = a_race;
		std::ranges::transform(lower, lower.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
		for (const auto& s : a_anchors.creatureRaces) {
			if (!s.empty() && lower.find(s) != std::string::npos) {
				return true;
			}
		}
		return race && !race->HasKeywordString("ActorTypeNPC");
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
		const bool on = cfg.enabled && !IsPaused();
		const auto n = a_state.actors.size();
		if (n == 0) {
			a_state.failReason = "no actors";
			return false;
		}

		// --- 1. resolve actors and skeleton nodes ---
		float hMin = 1e9f, hMax = -1e9f;
		bool anyCreature = false;
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
			const bool fresh = npc != a.npcNode.get();
			if (fresh) {
				// 3D was (re)loaded: start from scratch on the new skeleton
				a.npcNode.reset(npc);
				a.boneCache.clear();
				a.targetCache.clear();
				for (auto& arm : a.arms) {
					arm = ArmState{};
				}
				a.spine = SpineState{};
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
			a.measuredHeight = a.height;
			if (fresh) {
				a.creature = IsCreature(actor.get(), _anchors, a.race);
				if (a.creature) {
					logger::info("thread {} actor {} ({:08X}) is a creature: race '{}', skeleton scale {:.3f}, root '{}'", a_state.threadID, ai, a.formID,
						a.race, a.measuredHeight, root->name.c_str());
				}
			}
			if (a.creature) {
				anyCreature = true;
				if (cfg.creatureHeight == 0) {
					a.height = 1.0f;
				}
			}
			// world-space effect of what we wrote last frame (the bones we read were posed with it)
			a.prevWorld = (a.parent->world.rotate * a.appliedLocal) * a.parent->world.scale;
			if (a.creature) {
				// the creature is the reference of the scene: never moved, no heels, no IK
				a.heelWorld = {};
				a.restoreLocal = 0.0f;
				a.restoreWorld = {};
				a.shoeHeel = 0.0f;
				a.supportHeight = 0.0f;
				a.grounded = true;
				a.groundedLatch = true;
				a.lying = a.lyingLatch = false;
				a.kneeling = a.kneelingLatch = false;
				a.kneelRiseMax = 0.0f;
				continue;
			}
			// heels: NiOverride lifts the whole skeleton through the same node. The animation was made
			// without that lift, so it is part of the error just like the scale (OStim's heel removal
			// cancels it, then this is ~0).
			a.heelWorld = {};
			RE::NiPoint3 baseLocal{};
			{
				const auto& t = npc->local.translate;
				baseLocal = (a.written && Near(t, a.lastWritten)) ? a.lastWritten - a.appliedLocal : t;
				if (baseLocal.SqrLength() < 0.0001f || baseLocal.Length() > 40.0f) {
					baseLocal = {};  // nothing, or not a heel offset
				}
			}
			if (cfg.heelAware) {
				a.heelWorld = (a.parent->world.rotate * baseLocal) * a.parent->world.scale;
			}
			if (--a.shoeTimer <= 0) {
				a.shoeTimer = 30;
				a.shoeHeel = ShoeHeel(actor.get());
			}

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
			// OStim removes the heel lift in scenes not flagged "feetOnGround" (many standing scenes of
			// some packs), which sinks the shoes into the floor. Give it back while the actor stands on
			// its feet; the alignment below then treats it like any other heel lift.
			float wantRestore = 0.0f;
			if (on && cfg.heelAware && cfg.restoreHeels && a.shoeHeel > 0.5f && a.grounded && !a.kneeling && !a.lying) {
				wantRestore = std::max(0.0f, a.shoeHeel - baseLocal.z);
				if (wantRestore < 0.5f) {
					wantRestore = 0.0f;
				}
			}
			{
				const float k = 1.0f - std::exp(-a_delta / 0.25f);
				a.restoreLocal += (wantRestore - a.restoreLocal) * k;
				if (wantRestore == 0.0f && a.restoreLocal < 0.05f) {
					a.restoreLocal = 0.0f;
				}
			}
			a.restoreWorld = (a.parent->world.rotate * RE::NiPoint3{ 0.0f, 0.0f, a.restoreLocal }) * a.parent->world.scale;
			if (cfg.heelAware) {
				a.heelWorld += a.restoreWorld;
			}
			hMin = std::min(hMin, a.height);
			hMax = std::max(hMax, a.height);
		}
		if (anyCreature && !cfg.creatures) {
			for (auto& a : a_state.actors) {
				Restore(a);
				a.currentWorld = {};
			}
			a_state.failReason = "creature scene, creature support is off ([Creatures] bEnabled = 0)";
			return false;
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
			// error = actual position - where the bone would be at reference height without heels
			auto err = [&](const ActorState& s, RE::NiAVObject* bone) {
				const bool scaled = std::fabs(s.height - 1.0f) >= cfg.minHeightDiff;
				const bool heeled = s.heelWorld.SqrLength() > 0.0f;
				if (!scaled && !heeled) {
					return RE::NiPoint3{};
				}
				const float h = scaled ? s.height : 1.0f;
				// prevWorld removed everything we wrote; the heel we restore is part of the pose again
				const auto rel = bone->world.translate - s.prevWorld - s.root->world.translate + s.restoreWorld;
				return rel - (rel - s.heelWorld) / h;
			};
			rels.push_back({ c.a, c.b, c.weight, -(err(A, boneA) - err(B, boneB)) });
		}

		// --- 3. weighted least squares for one offset per actor (Gauss-Seidel, per axis) ---
		// mobility 0 = pinned on that axis; horizontal and vertical are handled separately so a
		// grounded actor can still slide on the floor.
		std::vector<float> mobH(n, 1.0f), mobBase(n, 1.0f), mobZ(n, 1.0f);
		constexpr float kPinned = 0.02f;
		const auto humans = std::ranges::count_if(a_state.actors, [](const ActorState& s) { return !s.creature; });
		for (size_t i = 0; i < n; ++i) {
			const auto& a = a_state.actors[i];
			if (a.creature) {
				mobH[i] = mobBase[i] = mobZ[i] = 0.0f;
				continue;
			}
			float m = 1.0f;
			switch (cfg.moveMode) {
			case MoveMode::kShorter:
				m = (hMax - hMin > 0.001f && a.height >= hMax - 0.001f) ? kPinned : 1.0f;
				break;
			case MoveMode::kTaller:
				m = (hMax - hMin > 0.001f && a.height <= hMin + 0.001f) ? kPinned : 1.0f;
				break;
			case MoveMode::kNPCsOnly:
				m = (a.isPlayer && humans > 1) ? kPinned : 1.0f;
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
				if (a_state.actors[i].lying || a_state.actors[i].creature) {
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
			if (on) {
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
			// what gets written also lifts the heels back; the leg IK only gets the alignment part, so
			// the feet end up on the floor at heel height instead of being planted where OStim sank them
			const auto writeLocal = local + RE::NiPoint3{ 0.0f, 0.0f, a.restoreLocal };

			// Leave the skeleton completely alone when there is nothing to apply, so other skeleton
			// editors (Fabulous Poser etc.) aren't overwritten every frame. Remove our offset once
			// when it has faded out.
			const bool nothing = a.currentWorld.SqrLength() < 1e-4f && a.restoreLocal == 0.0f;
			if (a.currentWorld.SqrLength() < 1e-4f) {
				a.currentWorld = {};
			}
			if (!nothing || a.written) {
				auto& t = a.npcNode->local.translate;
				const RE::NiPoint3 base = (a.written && Near(t, a.lastWritten)) ? a.lastWritten - a.appliedLocal : t;
				if (nothing) {
					t = base;
					a.written = false;
					a.appliedLocal = {};
				} else {
					t = base + writeLocal;
					a.lastWritten = t;
					a.appliedLocal = writeLocal;
					a.written = true;
				}
				RE::NiUpdateData ud{};
				a.npcNode->Update(ud);
			}

			const bool kneelIK = cfg.kneelLift && a.kneeling && a.currentWorld.z > 0.3f;
			const bool standIK = !a.kneeling && !a.lying && a.currentWorld.z < -0.3f;
			if (on && cfg.legIK && a.grounded && (kneelIK || standIK)) {
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

		UpdateArms(a_state, a_delta);

		if (cfg.mode == Mode::kStage && !a_state.sampleLogged && a_state.timer >= cfg.settleSeconds + cfg.sampleSeconds) {
			a_state.sampleLogged = true;
			std::string s;
			for (size_t i = 0; i < n; ++i) {
				const auto& a = a_state.actors[i];
				const auto avg = a_state.samples ? a.sampleSum / static_cast<float>(a_state.samples) : RE::NiPoint3{};
				if (a.creature) {
					s += std::format(" [{}] creature '{}' scale={:.3f} h={:.3f} (reference, not moved)", i, a.race, a.measuredHeight, a.height);
					continue;
				}
				s += std::format(" [{}] h={:.3f} heel={:.1f} shoes={:.1f} restored={:.1f}{} offset={}", i, a.height, a.heelWorld.z, a.shoeHeel, a.restoreLocal, a.lying ? " lying" : a.grounded ? (a.kneeling ? " kneeling" : " grounded") : " lifted", Fmt(avg));
			}
			logger::info("thread {} node '{}' settled ({} samples, {} rules):{}", a_state.threadID, a_state.nodeID, a_state.samples, rels.size(), s);
			if (anyCreature) {
				// which bones the rules used, and where creature bones sit in the creature's own frame
				// (to tune creature bone lists and the measured-height mode)
				auto where = [](const ActorState& st, RE::NiAVObject* b) -> std::string {
					if (!b) {
						return "-";
					}
					if (!st.creature) {
						return std::format("'{}'", b->name.c_str());
					}
					const float sc = st.root->world.scale > 0.0001f ? st.root->world.scale : 1.0f;
					const auto p = (st.root->world.rotate.Transpose() * (b->world.translate - st.root->world.translate)) / sc;
					return std::format("'{}'{}", b->name.c_str(), Fmt(p));
				};
				std::string rs;
				for (const auto& c : a_state.constraints) {
					auto& A = a_state.actors[c.a];
					auto& B = a_state.actors[c.b];
					rs += std::format(" {}[{}:{}={} -> {}:{}={}]", c.action, c.a, c.anchorA, where(A, FindBone(A, c.anchorA, _anchors)), c.b, c.anchorB,
						where(B, FindBone(B, c.anchorB, _anchors)));
				}
				for (size_t i = 0; i < n; ++i) {
					const auto& a = a_state.actors[i];
					for (size_t k = 0; k < 2; ++k) {
						if (a.arms[k].weight > 0.0f && a.arms[k].targetBone) {
							rs += std::format(" {}{}hand->{}('{}', err={:.1f})", i, k ? "R" : "L", a.arms[k].target, a.arms[k].targetBone->name.c_str(), a.arms[k].lastError);
						}
					}
				}
				logger::info("thread {} node '{}' creature scene bones:{}", a_state.threadID, a_state.nodeID, rs.empty() ? " (no rules)"s : rs);
			}
		}

		if (cfg.verbose) {
			a_state.verboseTimer += a_delta;
			if (a_state.verboseTimer >= 1.0f) {
				a_state.verboseTimer = 0.0f;
				std::string s;
				for (size_t i = 0; i < n; ++i) {
					const auto& a = a_state.actors[i];
					s += std::format(" [{}] h={:.3f} support={:.1f}{} solved={} applied={}", i, a.height, a.supportHeight, a.lying ? " lying"s : a.grounded ? (a.kneeling ? std::format(" kneeling(rise<={:.1f})", a.kneelRiseMax) : " grounded"s) : " lifted"s, Fmt(solved[i]), Fmt(a.currentWorld));
					for (size_t k = 0; k < 2; ++k) {
						if (a.arms[k].weight > 0.0f) {
							s += std::format(" {}hand->{}(w={:.2f} shift={} gap={} err={:.1f})", k ? "R" : "L", a.arms[k].target, a.arms[k].weight,
								Fmt(a.arms[k].delta), Fmt(a.arms[k].gap), a.arms[k].lastError);
							s += std::format("[swivel={:.0f} wrist={:.0f} lean={:.0f}]", a.arms[k].swivel * 57.3f, a.arms[k].wristBend * 57.3f, a.spine.angle * 57.3f);
						}
					}
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
