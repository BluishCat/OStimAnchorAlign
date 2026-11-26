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
		float kneelTorsoUp = 15.0f;
		bool verbose = false;

		// Re-reads Data/SKSE/Plugins/OStimAnchorAlign.ini. Cheap; called on every scene change.
		static Settings Load();
	};
}
