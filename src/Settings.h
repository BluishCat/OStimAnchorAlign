#pragma once

namespace OAA
{
	enum class Mode : int
	{
		kRealtime = 0,  // follow the per-frame correction (smoothed)
		kStage = 1      // average the correction over the first seconds of a stage, then hold it
	};

	enum class MoveMode : int
	{
		kSplit = 0,    // every actor moves an equal share
		kShorter = 1,  // shorter actors move, the tallest stays put
		kTaller = 2,   // taller actors move, the shortest stays put
		kNPCsOnly = 3  // the player stays put
	};

	struct Settings
	{
		bool enabled = true;
		Mode mode = Mode::kStage;
		float settleSeconds = 0.4f;
		float sampleSeconds = 1.5f;
		float smoothSeconds = 0.25f;
		MoveMode moveMode = MoveMode::kSplit;
		float strength = 1.0f;
		float horizontalScale = 1.0f;
		float verticalScale = 1.0f;
		float maxOffset = 40.0f;
		float minHeightDiff = 0.01f;
		float groundedHeight = 20.0f;
		float groundedZMobility = 0.0f;
		bool legIK = true;
		float maxLegDrop = 25.0f;
		bool kneelLift = true;
		float kneelRiseShare = 0.75f;
		float kneelHeight = 12.0f;
		float kneelMinHipHeight = 18.0f;
		float maxKneelRise = 15.0f;
		float lyingHeight = 22.0f;
		bool armIK = true;
		float handReach = 15.0f;
		float maxHandShift = 40.0f;
		float handSmoothSeconds = 0.12f;
		float wristFollow = 0.0f;
		float maxWristAngle = 45.0f;
		float elbowSwivel = 60.0f;
		float spineLean = 0.5f;
		float maxSpineLean = 25.0f;
		float kneelTorsoUp = 15.0f;
		float breastUp = 3.0f;       // extra hand offset on breast targets (target's up axis, units at height 1.0)
		float breastForward = 0.0f;  // ... and forward axis (away from the chest)
		bool heelAware = true;  // treat high-heel lift as part of the height difference
		bool restoreHeels = true;  // give the heel lift back to standing actors when OStim removed it
		bool verbose = false;
		int updateHook = 1;  // 1 = run in Main::Update (main thread), 0 = directly in the HUD advance (legacy)
		uint32_t pauseKey = 0xC5;       // DirectInput scan code, 0 = none
		uint32_t pauseModifier = 0x2A;  // must be held with pauseKey, 0 = none
		int language = 0;               // menu language: 0 = auto (game language), 1 = English, 2 = Japanese

		// Re-reads Data/SKSE/Plugins/OStimAnchorAlign.ini. Cheap; called on every scene change.
		static Settings Load();

		// Writes every value back to the INI (comments and layout are kept).
		void Save() const;
	};
}
