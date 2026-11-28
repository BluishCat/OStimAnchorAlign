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

		// hand target name -> bone groups; the first group with any existing bone is used, and every
		// existing bone of that group is a candidate (the hand goes to the nearest one)
		std::unordered_map<std::string, std::vector<std::vector<std::string>>> handTargets;
		// lower-case OStim action id -> hand target name (the action actor's hand reaches the target's part)
		std::unordered_map<std::string, std::string> handActions;

		// Creatures (OCreatures etc.) have their own skeletons. For a creature actor these lists are
		// tried first, then the normal ones; creatureBones["fallback"] is the last resort for any anchor.
		std::unordered_map<std::string, std::vector<std::string>> creatureBones;
		std::unordered_map<std::string, std::vector<std::vector<std::string>>> creatureHandTargets;
		// lower-case substrings of race editor IDs that count as creatures even though the race has
		// the ActorTypeNPC keyword (werewolves, vampire lords)
		std::vector<std::string> creatureRaces;

		// Loads Data/SKSE/Plugins/OStimAnchorAlign/anchors.json; falls back to built-in defaults.
		static Anchors Load();
	};
}
