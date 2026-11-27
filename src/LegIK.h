#pragma once

namespace OAA::LegIK
{
	struct Result
	{
		bool applied = false;
		RE::NiMatrix3 calfLocal{};  // what we wrote into the left calf (to check it survives the frame)
		RE::NiAVObject* calf = nullptr;
		float footError = 0.0f;     // distance between the planted target and the solved foot (left leg)
	};

	enum class Mode
	{
		kStand,  // feet planted, body lowered: bend the knees (two-bone IK)
		kKneel   // knees and feet planted, body raised: swing the thighs up around the knees
	};

	// Bends both legs so the planted parts stay where the animation put them, while the whole body
	// is shifted by a_offsetLocal (the translation we added to the skeleton's "NPC" node).
	// Call after the actor's animation pose has been written for this frame.
	Result Apply(RE::NiAVObject* a_root, const RE::NiPoint3& a_offsetLocal, Mode a_mode, std::string* a_failInfo = nullptr);

	// Two-bone IK for a chain upper -> lower -> end (helper nodes in between are fine): moves the end
	// effector to a_target, keeping the joint bending the way the animation bends it (a_fallbackPole
	// when the chain is straight) and the end's world orientation. Returns the remaining distance.
	// a_endFollow: 0 = the end keeps its animated world orientation, 1 = it turns with the lower bone.
	// a_maxEndAngle (radians): the end never deviates more than this from its animated orientation
	// relative to the lower bone (keeps wrists from bending unnaturally).
	float Reach(RE::NiAVObject* a_upper, RE::NiAVObject* a_lower, RE::NiAVObject* a_end, const RE::NiPoint3& a_target,
		const RE::NiPoint3& a_fallbackPole, float a_endFollow = 0.0f, float a_maxEndAngle = 4.0f, float a_swivel = 0.0f,
		float* a_wristAngle = nullptr);

	bool IsAncestorOf(RE::NiAVObject* a_ancestor, RE::NiAVObject* a_node);

	// Rotates a_node (in world space, around its own position) so that the world point a_from moves
	// toward a_to by a_fraction of the angle between them, at most a_maxAngle radians. Returns the
	// angle applied.
	float SwingToward(RE::NiAVObject* a_node, const RE::NiPoint3& a_from, const RE::NiPoint3& a_to, float a_fraction, float a_maxAngle);
}
