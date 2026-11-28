#pragma once

namespace OAA::TestScene
{
	// Debug helpers for trying creature scenes without having to find a partner.
	// All calls are safe from any thread (the work is queued to the main thread).

	struct Choice
	{
		const char* en;
		const char* ja;
		const char* race;  // race editor ID
	};
	const std::vector<Choice>& Choices();

	// spawn a peaceful creature of Choices()[a_index] next to the player
	void Spawn(int a_index);
	// start an OStim scene: player + (console-selected actor, else the creature spawned last, else
	// the nearest creature within ~20 m)
	void Start();
	// delete every creature spawned by Spawn()
	void Cleanup();

	std::string Status();
}
