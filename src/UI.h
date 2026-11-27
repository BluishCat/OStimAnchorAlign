#pragma once

namespace OAA::UI
{
	// Registers the settings page with SKSE Menu Framework if it is installed (call at kPostLoad).
	// Without the framework this does nothing and the INI stays the only way to configure the mod.
	void Register();

	// true while the menu is waiting for a key to bind (the pause hotkey must not fire then)
	bool IsCapturingKey();

	// true while any SKSE Menu Framework window that takes input is open
	bool IsMenuOpen();
}
