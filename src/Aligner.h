#pragma once

#include "Anchors.h"
#include "Settings.h"

namespace OAA
{
	// What we copy out of OStim inside its listener callbacks, so the main-thread update
	// never has to touch OStim objects.
	struct SceneSnapshot
	{
		struct RawAction
		{
			std::string id;
			int32_t actor = -1;
			int32_t target = -1;
		};

		int32_t threadID = -1;
		std::string nodeID;
		std::vector<RE::ActorHandle> actors;
		std::vector<RawAction> actions;
	};

	class Aligner
	{
	public:
		static Aligner& Get()
		{
			static Aligner instance;
			return instance;
		}

		// any thread
		void OnNodeChanged(SceneSnapshot a_snapshot);
		void OnThreadStop(int32_t a_threadID);

		// main thread, once per frame
		void Update(float a_delta);

		// main thread: drop every offset (used when the plugin gets disabled entirely)
		void RestoreAll();

		// after an actor's animation update: bend the legs of actors we lowered
		void OnActorAnimated(RE::Actor* a_actor);

		// hotkey pause: while paused every correction fades out and the skeleton is left untouched
		bool TogglePause() { return _paused = !_paused; }
		bool IsPaused() const { return _paused.load(std::memory_order_relaxed); }

		// apply changed settings to the scenes that are running right now (menu)
		void ApplySettings(const Settings& a_settings);

	private:
		struct ArmState
		{
			RE::NiPointer<RE::NiAVObject> upper, lower, end, finger;
			bool resolved = false;
			bool written = false;
			RE::NiMatrix3 origU{}, origL{}, origE{};   // animation pose before our edit
			RE::NiMatrix3 wroteU{}, wroteL{}, wroteE{}; // what we wrote
			RE::NiPoint3 delta{};                       // smoothed hand correction (world)
			float weight = 0.0f;
			std::string target;                         // for the log
			RE::NiPointer<RE::NiAVObject> targetBone;   // bone the hand aims at (log)
			float lastError = 0.0f;
			RE::NiPoint3 gap{};  // hand - target at reference height (log)
			float swivel = 0.0f;
			float wristBend = 0.0f;
		};

		struct SpineState
		{
			std::array<RE::NiPointer<RE::NiAVObject>, 2> bones;  // Spine1, Spine2
			bool resolved = false;
			bool written = false;
			std::array<RE::NiMatrix3, 2> orig{}, wrote{};
			float angle = 0.0f;  // log
		};

		struct ActorState
		{
			RE::ActorHandle handle;
			RE::FormID formID = 0;
			RE::NiPointer<RE::NiAVObject> npcNode;
			std::unordered_map<std::string, RE::NiPointer<RE::NiAVObject>> boneCache;
			bool written = false;
			RE::NiPoint3 appliedLocal{};  // our share of npcNode->local.translate
			RE::NiPoint3 lastWritten{};   // full local.translate we wrote last time
			RE::NiPoint3 currentWorld{};  // smoothed world-space offset in effect
			RE::NiPoint3 sampleSum{};

			// per-frame scratch
			RE::NiAVObject* root = nullptr;
			RE::NiAVObject* parent = nullptr;
			RE::NiPoint3 prevWorld{};
			RE::NiPoint3 heelWorld{};  // world-space heel lift in effect (NiOverride's + the part we restore)
			RE::NiPoint3 restoreWorld{};  // world-space heel lift we add back (OStim removed it, actor stands)
			float shoeHeel = 0.0f;        // heel height of the worn shoes (HH_OFFSET / SDTA), NPC-local units
			float restoreLocal = 0.0f;    // smoothed heel restore, parent-local z
			int shoeTimer = 0;            // frames until the shoes are checked again
			float height = 1.0f;
			float measuredHeight = 1.0f;  // skeleton scale before the creature override
			bool isPlayer = false;
			bool creature = false;        // non-human skeleton/race: never moved, the reference of the scene
			std::string race;             // race editor ID (log)
			float supportHeight = 0.0f;  // lowest foot/calf bone above the actor's base
			bool grounded = true;
			bool groundedLatch = false;  // grounded at some point in this stage
			bool ikFailLogged = false;
			bool kneeling = false;       // knees on the floor with the hips up (kneeling / sitting on heels)
			bool kneelingLatch = false;
			bool lying = false;          // pelvis or chest on the floor
			bool lyingLatch = false;
			float kneelRiseMax = 0.0f;   // how far the hips can rise before the thighs are upright
			std::array<ArmState, 2> arms;
			SpineState spine;
			std::unordered_map<std::string, std::vector<RE::NiPointer<RE::NiAVObject>>> targetCache;
		};

		struct HandRule
		{
			int32_t a = -1;  // whose hands
			int32_t b = -1;  // whose body part
			std::string target;
			std::string action;
		};

		struct Constraint
		{
			int32_t a = -1;
			int32_t b = -1;
			std::string anchorA;
			std::string anchorB;
			float weight = 1.0f;
			std::string action;
		};

		struct ThreadState
		{
			int32_t threadID = -1;
			std::string nodeID;
			Settings settings;
			std::vector<ActorState> actors;
			std::vector<Constraint> constraints;
			std::vector<HandRule> handRules;
			float timer = 0.0f;
			uint32_t samples = 0;
			bool sampleLogged = false;
			float verboseTimer = 0.0f;
			int lastOk = -1;  // -1 unknown, 0 skipped, 1 updating
			std::string failReason;
		};

		Aligner() = default;

		bool UpdateThread(ThreadState& a_state, float a_delta);
		static RE::NiAVObject* FindBone(ActorState& a_actor, const std::string& a_anchor, const Anchors& a_anchors);
		static bool IsCreature(RE::Actor* a_actor, const Anchors& a_anchors, std::string& a_race);
		void Restore(ActorState& a_actor);
		void UpdateArms(ThreadState& a_state, float a_delta);
		static void ReleaseArm(ArmState& a_arm);
		static void ReleaseSpine(SpineState& a_spine);
		const std::vector<RE::NiPointer<RE::NiAVObject>>& HandTargets(ActorState& a_actor, const std::string& a_name);
		bool _armLogged = false;
		void SetIK(RE::FormID a_id, const RE::NiPoint3& a_offsetLocal, bool a_kneel);
		void ClearIK(RE::FormID a_id);

		struct IKEntry
		{
			RE::NiPoint3 offsetLocal{};
			bool kneel = false;    // IK mode used last time
			bool applied = false;  // the anim hook bent the legs at least once for this request
			RE::NiPointer<RE::NiAVObject> calf;
			RE::NiMatrix3 calfLocal{};
			float footError = 0.0f;
			uint32_t persisted = 0;
			uint32_t overwritten = 0;
		};
		std::mutex _ikLock;
		std::unordered_map<RE::FormID, IKEntry> _ik;
		std::atomic<size_t> _ikCount{ 0 };
		std::atomic<uint32_t> _animCalls{ 0 };
		std::atomic<bool> _paused{ false };
		bool _ikLogged = false;

		std::mutex _lock;
		std::unordered_map<int32_t, ThreadState> _threads;
		std::vector<ActorState> _pendingRestore;
		Anchors _anchors;
		bool _anchorsLoaded = false;
	};
}
