#pragma once

namespace OAA
{
	// One alignment rule: "for this OStim action, keep <actor anchor> and <target anchor>
	// in the same relation they would have if both actors had the animation's reference height".
	struct ActionRule
	{
		std::string actorAnchor;   // anchor name, see Anchors::bones
		std::string targetAnchor;  //
		float weight = 1.0f;
	};

	struct Anchors
	{
		// anchor name -> bone names, first one found on the skeleton wins
		std::unordered_map<std::string, std::vector<std::string>> bones;
		// lower-case OStim action id -> rule
		std::unordered_map<std::string, ActionRule> actions;

		// Loads Data/SKSE/Plugins/OStimAnchorAlign/anchors.json; falls back to built-in defaults.
		static Anchors Load();
	};
}
