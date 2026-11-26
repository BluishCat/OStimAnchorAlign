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
}
