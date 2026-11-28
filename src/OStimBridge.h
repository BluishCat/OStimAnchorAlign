#pragma once

namespace OAA::Bridge
{
	// Call at kPostPostLoad: gets OStim's plugin interface and registers the listeners.
	bool Connect();

	// Starts an OStim scene with these actors (OStim picks a fitting start node). Main thread.
	// Returns the thread id, or -1.
	int32_t StartScene(std::vector<RE::Actor*> a_actors);
	// true while OStim runs the thread with this id
	bool IsThreadRunning(int32_t a_threadID);
}
