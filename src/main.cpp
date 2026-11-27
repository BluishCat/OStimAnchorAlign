#include "Aligner.h"
#include "OStimBridge.h"
#include "UI.h"

namespace
{
	float FrameDelta()
	{
		static auto last = std::chrono::steady_clock::now();
		const auto now = std::chrono::steady_clock::now();
		const float dt = std::chrono::duration<float>(now - last).count();
		last = now;
		return dt;
	}

	// The HUD advance runs on whatever job thread Scaleform uses this frame, i.e. in parallel
	// with animation, CBPC/SMP physics and other skeleton editors. Writing bones from there
	// makes the order random and the result jitter, so by default the work runs in Main::Update
	// on the main thread. (SKSE tasks were tried too: they also run on worker threads.)
	std::thread::id g_mainThread{};
	bool g_useTask = true;
	std::atomic<uint32_t> g_offMain{ 0 }, g_onMain{ 0 };

	void RunUpdate()
	{
		(std::this_thread::get_id() == g_mainThread ? g_onMain : g_offMain).fetch_add(1, std::memory_order_relaxed);
		static auto lastLog = std::chrono::steady_clock::now();
		if (const auto now = std::chrono::steady_clock::now(); now - lastLog >= 30s) {
			lastLog = now;
			logger::info("update thread check: {} on the main thread, {} elsewhere (last 30s, mode {})", g_onMain.exchange(0), g_offMain.exchange(0),
				g_useTask ? "Main::Update" : "HUD advance");
		}
		OAA::Aligner::Get().Update(FrameDelta());
	}

	void RequestUpdate()
	{
		if (!g_useTask) {
			RunUpdate();  // legacy: run right here in the HUD advance
		}
		// otherwise Main::Update does the work
	}

	// Primary: the call inside Main::Update that runs once per frame on the main thread
	// (the same spot many SKSE plugins use). PlayerCharacter::Update can't be used: it stops
	// being called while OStim runs a player scene. Verified at runtime before patching,
	// because a wrong offset would corrupt code.
	struct MainUpdateHook
	{
		// pass the integer argument registers and the return value through untouched
		static std::uintptr_t thunk(std::uintptr_t a1, std::uintptr_t a2, std::uintptr_t a3, std::uintptr_t a4)
		{
			const auto result = func(a1, a2, a3, a4);
			RunUpdate();  // already on the main thread
			return result;
		}
		static inline REL::Relocation<decltype(thunk)> func;

		static bool Install()
		{
			REL::Relocation<std::uintptr_t> target{ REL::RelocationID(35565, 36564), REL::VariantOffset(0x748, 0xC26, 0x7EE) };
			const auto addr = target.address();

			const auto& module = REL::Module::get();
			const auto text = module.segment(REL::Segment::textx);
			const auto textBegin = text.address();
			const auto textEnd = textBegin + text.size();

			const auto opcode = *reinterpret_cast<const std::uint8_t*>(addr);
			const auto rel = *reinterpret_cast<const std::int32_t*>(addr + 1);
			const auto callee = addr + 5 + static_cast<std::intptr_t>(rel);
			logger::info("Main::Update call site +{:X}: opcode {:02X}, callee +{:X}", addr - module.base(), opcode, callee - module.base());
			// the callee may be another plugin's trampoline (this call site is a popular hook), so
			// only the opcode is checked; write_call chains to whatever is there
			(void)textBegin;
			(void)textEnd;
			(void)callee;
			if (opcode != 0xE8) {
				logger::warn("Main::Update call site doesn't look like a CALL, not patching it");
				return false;
			}

			func = SKSE::GetTrampoline().write_call<5>(addr, thunk);
			logger::info("installed Main::Update hook");
			return true;
		}
	};

	// Fallback: HUD menu frame advance (runs every frame while the HUD exists).
	struct HUDAdvanceHook
	{
		static void thunk(RE::IMenu* a_this, float a_interval, std::uint32_t a_currentTime)
		{
			func(a_this, a_interval, a_currentTime);
			RequestUpdate();
		}
		static inline REL::Relocation<decltype(thunk)> func;

		static void Install()
		{
			REL::Relocation<std::uintptr_t> vtbl{ RE::VTABLE_HUDMenu[0] };
			func = vtbl.write_vfunc(0x5, thunk);
			logger::info("installed HUDMenu::AdvanceMovie hook (fallback)");
		}
	};

	// Character/PlayerCharacter::UpdateAnimation: runs after the animation pose is written,
	// which is where the leg IK has to bend the bones.
	template <class T>
	struct UpdateAnimationHook
	{
		static void thunk(T* a_this, float a_delta)
		{
			func(a_this, a_delta);
			OAA::Aligner::Get().OnActorAnimated(a_this);
		}
		static inline REL::Relocation<decltype(thunk)> func;

		static void Install(REL::VariantID a_vtbl, const char* a_name)
		{
			REL::Relocation<std::uintptr_t> vtbl{ a_vtbl };
			func = vtbl.write_vfunc(0x7D, thunk);
			logger::info("installed {}::UpdateAnimation hook", a_name);
		}
	};

	// toggles the pause hotkey (iPauseKey); the key is re-read from the INI on every press
	class PauseKeySink : public RE::BSTEventSink<RE::InputEvent*>
	{
	public:
		static PauseKeySink* Get()
		{
			static PauseKeySink sink;
			return &sink;
		}

		RE::BSEventNotifyControl ProcessEvent(RE::InputEvent* const* a_event, RE::BSTEventSource<RE::InputEvent*>*) override
		{
			if (!a_event) {
				return RE::BSEventNotifyControl::kContinue;
			}
			for (auto e = *a_event; e; e = e->next) {
				if (e->GetEventType() != RE::INPUT_EVENT_TYPE::kButton || e->GetDevice() != RE::INPUT_DEVICE::kKeyboard) {
					continue;
				}
				const auto button = e->AsButtonEvent();
				if (!button || !button->IsDown()) {
					continue;
				}
				const auto cfg = OAA::Settings::Load();
				if (cfg.pauseKey == 0 || button->GetIDCode() != cfg.pauseKey) {
					continue;
				}
				if (cfg.pauseModifier != 0) {
					// scan code -> virtual key (distinguishes left/right Shift, Ctrl, Alt)
					UINT vk = ::MapVirtualKeyW(cfg.pauseModifier & 0x7F, MAPVK_VSC_TO_VK_EX);
					if (cfg.pauseModifier == 0x9D) {
						vk = VK_RCONTROL;
					} else if (cfg.pauseModifier == 0xB8) {
						vk = VK_RMENU;
					}
					if (vk == 0 || (::GetAsyncKeyState(static_cast<int>(vk)) & 0x8000) == 0) {
						continue;
					}
				}
				if (OAA::UI::IsCapturingKey() || OAA::UI::IsMenuOpen()) {
					continue;
				}
				if (auto ui = RE::UI::GetSingleton(); ui && (ui->IsMenuOpen(RE::Console::MENU_NAME) || ui->GameIsPaused())) {
					continue;
				}
				const bool paused = OAA::Aligner::Get().TogglePause();
				logger::info("correction {} by hotkey", paused ? "paused" : "resumed");
				RE::SendHUDMessage::ShowHUDMessage(paused ? "OStim Anchor Align: paused" : "OStim Anchor Align: resumed");
			}
			return RE::BSEventNotifyControl::kContinue;
		}
	};

	bool g_connected = false;

	void OnMessage(SKSE::MessagingInterface::Message* a_msg)
	{
		if (!a_msg) {
			return;
		}
		switch (a_msg->type) {
		case SKSE::MessagingInterface::kPostPostLoad:
			g_mainThread = std::this_thread::get_id();  // plugin messages arrive on the main thread
			g_useTask = OAA::Settings::Load().updateHook == 1;
			logger::info("per-frame work runs {}", g_useTask ? "in Main::Update (main thread)" : "directly in the HUD advance (legacy)");
			g_connected = OAA::Bridge::Connect();
			if (g_connected) {
				// Main::Update (main thread) by default; the HUD advance only as a fallback.
				if (!g_useTask || !MainUpdateHook::Install()) {
					g_useTask = false;
					HUDAdvanceHook::Install();
				}
				OAA::UI::Register();
				UpdateAnimationHook<RE::Character>::Install(RE::VTABLE_Character[0], "Character");
				UpdateAnimationHook<RE::PlayerCharacter>::Install(RE::VTABLE_PlayerCharacter[0], "PlayerCharacter");
			}
			break;
		case SKSE::MessagingInterface::kInputLoaded:
			if (g_connected) {
				if (auto input = RE::BSInputDeviceManager::GetSingleton()) {
					input->AddEventSink(PauseKeySink::Get());
					logger::info("pause hotkey listener registered");
				}
			}
			break;
		case SKSE::MessagingInterface::kPreLoadGame:
			if (g_connected) {
				// 3D is rebuilt on load; just forget everything
				SKSE::GetTaskInterface()->AddTask([]() { OAA::Aligner::Get().RestoreAll(); });
			}
			break;
		default:
			break;
		}
	}
}

SKSEPluginLoad(const SKSE::LoadInterface* a_skse)
{
	SKSE::Init(a_skse, { .log = true, .logLevel = spdlog::level::info, .trampoline = true, .trampolineSize = 64 });
	logger::info("{} {} loading", SKSE::GetPluginName(), SKSE::GetPluginVersion().string());

	const auto messaging = SKSE::GetMessagingInterface();
	if (!messaging || !messaging->RegisterListener(OnMessage)) {
		logger::error("failed to register the SKSE message listener");
		return false;
	}
	return true;
}
