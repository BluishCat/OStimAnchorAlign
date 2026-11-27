#include "UI.h"

#include "Aligner.h"
#include "Settings.h"

#include "SKSEMenuFramework.h"

namespace OAA::UI
{
	namespace
	{
		using namespace ImGuiMCP;

		Settings g_cfg;
		bool g_dirty = false;
		std::chrono::steady_clock::time_point g_lastChange{};
		std::atomic<bool> g_capturing{ false };
		bool g_registered = false;

		// ---------- language ----------

		bool Japanese()
		{
			if (g_cfg.language == 1) {
				return false;
			}
			if (g_cfg.language == 2) {
				return true;
			}
			static int autoJa = -1;
			if (autoJa < 0) {
				autoJa = 0;
				if (auto ini = RE::INISettingCollection::GetSingleton()) {
					if (auto s = ini->GetSetting("sLanguage:General"); s && s->GetString()) {
						autoJa = _stricmp(s->GetString(), "JAPANESE") == 0 ? 1 : 0;
					}
				}
			}
			return autoJa == 1;
		}

		const char* T(const char* a_en, const char* a_ja) { return Japanese() ? a_ja : a_en; }

		// visible label + stable ImGui id (labels change with the language)
		std::string L(const char* a_en, const char* a_ja, const char* a_id) { return std::format("{}##{}", T(a_en, a_ja), a_id); }

		// ---------- key names ----------

		std::string KeyName(uint32_t a_code)
		{
			static const std::unordered_map<uint32_t, const char*> names{
				{ 0x01, "Esc" }, { 0x0E, "Backspace" }, { 0x0F, "Tab" }, { 0x1C, "Enter" }, { 0x1D, "L-Ctrl" }, { 0x2A, "L-Shift" },
				{ 0x36, "R-Shift" }, { 0x38, "L-Alt" }, { 0x39, "Space" }, { 0x3A, "Caps Lock" }, { 0x3B, "F1" }, { 0x3C, "F2" },
				{ 0x3D, "F3" }, { 0x3E, "F4" }, { 0x3F, "F5" }, { 0x40, "F6" }, { 0x41, "F7" }, { 0x42, "F8" }, { 0x43, "F9" },
				{ 0x44, "F10" }, { 0x45, "Num Lock" }, { 0x46, "Scroll Lock" }, { 0x47, "Num 7" }, { 0x48, "Num 8" }, { 0x49, "Num 9" },
				{ 0x4A, "Num -" }, { 0x4B, "Num 4" }, { 0x4C, "Num 5" }, { 0x4D, "Num 6" }, { 0x4E, "Num +" }, { 0x4F, "Num 1" },
				{ 0x50, "Num 2" }, { 0x51, "Num 3" }, { 0x52, "Num 0" }, { 0x53, "Num ." }, { 0x57, "F11" }, { 0x58, "F12" },
				{ 0x9C, "Num Enter" }, { 0x9D, "R-Ctrl" }, { 0xB5, "Num /" }, { 0xB7, "Print Screen" }, { 0xB8, "R-Alt" },
				{ 0xC5, "Pause" }, { 0xC7, "Home" }, { 0xC8, "Up" }, { 0xC9, "Page Up" }, { 0xCB, "Left" }, { 0xCD, "Right" },
				{ 0xCF, "End" }, { 0xD0, "Down" }, { 0xD1, "Page Down" }, { 0xD2, "Insert" }, { 0xD3, "Delete" },
				{ 0x02, "1" }, { 0x03, "2" }, { 0x04, "3" }, { 0x05, "4" }, { 0x06, "5" }, { 0x07, "6" }, { 0x08, "7" },
				{ 0x09, "8" }, { 0x0A, "9" }, { 0x0B, "0" }, { 0x0C, "-" }, { 0x0D, "=" }, { 0x10, "Q" }, { 0x11, "W" },
				{ 0x12, "E" }, { 0x13, "R" }, { 0x14, "T" }, { 0x15, "Y" }, { 0x16, "U" }, { 0x17, "I" }, { 0x18, "O" },
				{ 0x19, "P" }, { 0x1A, "[" }, { 0x1B, "]" }, { 0x1E, "A" }, { 0x1F, "S" }, { 0x20, "D" }, { 0x21, "F" },
				{ 0x22, "G" }, { 0x23, "H" }, { 0x24, "J" }, { 0x25, "K" }, { 0x26, "L" }, { 0x27, ";" }, { 0x28, "'" },
				{ 0x29, "`" }, { 0x2B, "\\" }, { 0x2C, "Z" }, { 0x2D, "X" }, { 0x2E, "C" }, { 0x2F, "V" }, { 0x30, "B" },
				{ 0x31, "N" }, { 0x32, "M" }, { 0x33, "," }, { 0x34, "." }, { 0x35, "/" },
			};
			if (a_code == 0) {
				return "-";
			}
			if (auto it = names.find(a_code); it != names.end()) {
				return it->second;
			}
			return std::format("0x{:02X}", a_code);
		}

		bool IsModifier(uint32_t a_code)
		{
			return a_code == 0x2A || a_code == 0x36 || a_code == 0x1D || a_code == 0x9D || a_code == 0x38 || a_code == 0xB8;
		}

		uint32_t HeldModifier()
		{
			static const std::array<std::pair<int, uint32_t>, 6> mods{ std::pair{ VK_LSHIFT, 0x2Au }, std::pair{ VK_RSHIFT, 0x36u },
				std::pair{ VK_LCONTROL, 0x1Du }, std::pair{ VK_RCONTROL, 0x9Du }, std::pair{ VK_LMENU, 0x38u }, std::pair{ VK_RMENU, 0xB8u } };
			for (auto [vk, code] : mods) {
				if (::GetAsyncKeyState(vk) & 0x8000) {
					return code;
				}
			}
			return 0;
		}

		// ---------- change tracking ----------

		void Changed()
		{
			g_dirty = true;
			g_lastChange = std::chrono::steady_clock::now();
			Aligner::Get().ApplySettings(g_cfg);
		}

		void SaveIfDirty(bool a_force)
		{
			if (!g_dirty) {
				return;
			}
			if (!a_force && std::chrono::steady_clock::now() - g_lastChange < 400ms) {
				return;
			}
			g_cfg.Save();
			g_dirty = false;
		}

		void Help(const char* a_en, const char* a_ja)
		{
			SameLine();
			TextDisabled("(?)");
			if (IsItemHovered()) {
				SetTooltip("%s", T(a_en, a_ja));
			}
		}

		void Check(const char* a_en, const char* a_ja, const char* a_id, bool* a_value)
		{
			if (Checkbox(L(a_en, a_ja, a_id).c_str(), a_value)) {
				Changed();
			}
		}

		void Slider(const char* a_en, const char* a_ja, const char* a_id, float* a_value, float a_min, float a_max, const char* a_fmt = "%.2f")
		{
			if (SliderFloat(L(a_en, a_ja, a_id).c_str(), a_value, a_min, a_max, a_fmt, ImGuiSliderFlags_AlwaysClamp)) {
				Changed();
			}
		}

		template <class E>
		void EnumCombo(const char* a_en, const char* a_ja, const char* a_id, E* a_value, std::initializer_list<std::pair<const char*, const char*>> a_items)
		{
			std::vector<const char*> items;
			for (auto& [en, ja] : a_items) {
				items.push_back(T(en, ja));
			}
			int v = static_cast<int>(*a_value);
			if (Combo(L(a_en, a_ja, a_id).c_str(), &v, items.data(), static_cast<int>(items.size()))) {
				*a_value = static_cast<E>(v);
				Changed();
			}
		}

		// ---------- the page ----------

		void __stdcall Render()
		{
			SaveIfDirty(false);
			auto& aligner = Aligner::Get();

			// status + quick actions
			const bool paused = aligner.IsPaused();
			if (!g_cfg.enabled) {
				TextColored(ImVec4{ 1.0f, 0.5f, 0.4f, 1.0f }, "%s", T("Disabled", "無効"));
			} else if (paused) {
				TextColored(ImVec4{ 1.0f, 0.8f, 0.3f, 1.0f }, "%s", T("Paused", "一時停止中"));
			} else {
				TextColored(ImVec4{ 0.5f, 0.9f, 0.5f, 1.0f }, "%s", T("Active", "動作中"));
			}
			SameLine();
			if (Button(paused ? L("Resume", "再開", "pause").c_str() : L("Pause", "一時停止", "pause").c_str())) {
				aligner.TogglePause();
			}

			SeparatorText(T("General", "全般"));
			Check("Enable", "有効にする", "enabled", &g_cfg.enabled);

			// language
			{
				const char* items[] = { "Auto (game language)", "English", "日本語" };
				int lang = g_cfg.language;
				if (Combo("Language / 言語##lang", &lang, items, 3)) {
					g_cfg.language = lang;
					Changed();
				}
				Help("Japanese text needs \"EnableJapanese = true\" in SKSEMenuFramework.ini.",
					"日本語を表示するには、SKSEMenuFramework.ini で EnableJapanese = true にしてください。");
			}

			// hotkey
			{
				Text("%s", T("Pause hotkey", "一時停止キー"));
				SameLine();
				std::string current = g_cfg.pauseModifier ? KeyName(g_cfg.pauseModifier) + " + " + KeyName(g_cfg.pauseKey) : KeyName(g_cfg.pauseKey);
				if (g_capturing) {
					current = T("Press a key... (Esc: cancel)", "キーを押してください…（Esc：キャンセル）");
				}
				if (Button(std::format("{}##hotkey", current).c_str())) {
					g_capturing = true;
				}
				SameLine();
				if (Button(L("Clear", "解除", "hotkeyclear").c_str())) {
					g_cfg.pauseKey = 0;
					g_cfg.pauseModifier = 0;
					g_capturing = false;
					Changed();
				}
				Help("Click, then press the key. Hold Shift/Ctrl/Alt while pressing it to add a modifier.",
					"クリックしてからキーを押してください。Shift・Ctrl・Alt を押しながら押すと、同時押しのキーとして登録されます。");
			}

			SeparatorText(T("Body alignment", "体の位置合わせ"));
			EnumCombo("Mode", "補正方式", "mode", &g_cfg.mode,
				{ { "Realtime (every frame)", "毎フレーム追従" }, { "Per stage (steady)", "ステージごとに固定（安定）" } });
			EnumCombo("Who moves", "動かす側", "movemode", &g_cfg.moveMode,
				{ { "Split between all", "全員で分担" }, { "Shorter actors", "背の低い側" }, { "Taller actors", "背の高い側" }, { "NPCs only", "NPCだけ" } });
			Slider("Strength", "補正の強さ", "strength", &g_cfg.strength, 0.0f, 2.0f);
			Slider("Max offset", "移動の上限", "maxoffset", &g_cfg.maxOffset, 0.0f, 100.0f, "%.0f");
			if (CollapsingHeader(L("Advanced", "詳細", "advbody").c_str())) {
				Slider("Horizontal scale", "水平方向の倍率", "hscale", &g_cfg.horizontalScale, 0.0f, 2.0f);
				Slider("Vertical scale", "上下方向の倍率", "vscale", &g_cfg.verticalScale, 0.0f, 2.0f);
				Slider("Smoothing (s)", "なめらかさ（秒）", "smooth", &g_cfg.smoothSeconds, 0.0f, 2.0f);
				Slider("Settle time (s)", "開始待ち（秒）", "settle", &g_cfg.settleSeconds, 0.0f, 3.0f);
				Slider("Sample time (s)", "計測時間（秒）", "sample", &g_cfg.sampleSeconds, 0.1f, 5.0f);
				Slider("Standing: foot height", "立っている判定：足の高さ", "grounded", &g_cfg.groundedHeight, 0.0f, 60.0f, "%.0f");
				Slider("Lying: pelvis/chest height", "寝そべり判定：骨盤・胸の高さ", "lying", &g_cfg.lyingHeight, 0.0f, 60.0f, "%.0f");
			}

			SeparatorText(T("Leg IK", "脚のIK"));
			Check("Bend knees of tall standing actors", "背の高い側の膝を曲げる", "legik", &g_cfg.legIK);
			BeginDisabled(!g_cfg.legIK);
			Slider("Max lowering", "下げる量の上限", "maxdrop", &g_cfg.maxLegDrop, 0.0f, 60.0f, "%.0f");
			Check("Raise hips of kneeling actors", "膝立ちの側の腰を上げる", "kneellift", &g_cfg.kneelLift);
			BeginDisabled(!g_cfg.kneelLift);
			Slider("Kneeling actor's share", "膝立ち側の分担", "kneelshare", &g_cfg.kneelRiseShare, 0.05f, 0.95f);
			Slider("Max raise", "上げる量の上限", "maxrise", &g_cfg.maxKneelRise, 0.0f, 40.0f, "%.0f");
			EndDisabled();
			EndDisabled();

			SeparatorText(T("Arm IK", "腕のIK"));
			Check("Make touching hands reach", "触る手を相手に届かせる", "armik", &g_cfg.armIK);
			BeginDisabled(!g_cfg.armIK);
			Slider("Touch distance", "触っているとみなす距離", "reach", &g_cfg.handReach, 1.0f, 50.0f, "%.0f");
			Slider("Max hand shift", "手の移動の上限", "maxshift", &g_cfg.maxHandShift, 0.0f, 80.0f, "%.0f");
			Slider("Max wrist bend (deg)", "手首の曲がりの上限（度）", "wrist", &g_cfg.maxWristAngle, 0.0f, 180.0f, "%.0f");
			Slider("Elbow swivel (deg)", "肘の回転（度）", "swivel", &g_cfg.elbowSwivel, 0.0f, 120.0f, "%.0f");
			Slider("Upper-body lean", "上半身の傾き", "lean", &g_cfg.spineLean, 0.0f, 1.0f);
			Slider("Max lean (deg)", "傾きの上限（度）", "maxlean", &g_cfg.maxSpineLean, 0.0f, 60.0f, "%.0f");
			if (CollapsingHeader(L("Advanced", "詳細", "advarm").c_str())) {
				Slider("Hand follows forearm", "手が前腕についていく割合", "follow", &g_cfg.wristFollow, 0.0f, 1.0f);
				Slider("Hand smoothing (s)", "手のなめらかさ（秒）", "handsmooth", &g_cfg.handSmoothSeconds, 0.0f, 1.0f);
			}
			EndDisabled();

			SeparatorText(T("Other", "その他"));
			Check("Verbose log", "詳しいログ", "verbose", &g_cfg.verbose);
			if (Button(L("Reset to defaults", "標準値に戻す", "reset").c_str())) {
				const auto keep = std::tuple{ g_cfg.pauseKey, g_cfg.pauseModifier, g_cfg.language };
				g_cfg = Settings{};
				std::tie(g_cfg.pauseKey, g_cfg.pauseModifier, g_cfg.language) = keep;
				Changed();
			}
			SameLine();
			if (Button(L("Reload INI", "INIを読み直す", "reload").c_str())) {
				g_cfg = Settings::Load();
				g_dirty = false;
				Aligner::Get().ApplySettings(g_cfg);
			}
			TextDisabled("%s", T("Changes apply immediately and are saved to OStimAnchorAlign.ini.",
								   "変更はすぐに反映され、OStimAnchorAlign.ini に保存されます。"));
		}

		// key capture for the hotkey button
		bool __stdcall OnInput(RE::InputEvent* a_event)
		{
			if (!g_capturing || !a_event || a_event->GetDevice() != RE::INPUT_DEVICE::kKeyboard) {
				return false;
			}
			const auto button = a_event->AsButtonEvent();
			if (!button || !button->IsDown()) {
				return g_capturing.load();
			}
			const auto code = button->GetIDCode();
			if (code == 0x01) {  // Esc
				g_capturing = false;
				return true;
			}
			if (IsModifier(code)) {
				return true;  // wait for the main key
			}
			g_cfg.pauseKey = code;
			g_cfg.pauseModifier = HeldModifier();
			g_capturing = false;
			Changed();
			return true;
		}

		void __stdcall OnEvent(SKSEMenuFramework::Model::EventType a_type)
		{
			if (a_type == SKSEMenuFramework::Model::kCloseMenu) {
				g_capturing = false;
				SaveIfDirty(true);
			} else if (a_type == SKSEMenuFramework::Model::kOpenMenu) {
				if (!g_dirty) {
					g_cfg = Settings::Load();  // pick up manual INI edits
				}
			}
		}
	}

	void Register()
	{
		if (!SKSEMenuFramework::IsInstalled() || !GetMenuFrameworkModule()) {
			logger::info("SKSE Menu Framework not found; settings are INI only");
			return;
		}
		g_cfg = Settings::Load();
		SKSEMenuFramework::SetSection("OStim Anchor Align");
		SKSEMenuFramework::AddSectionItem("Settings", Render);
		SKSEMenuFramework::AddInputEvent(OnInput);
		SKSEMenuFramework::AddEvent(OnEvent, 0.0f);
		g_registered = true;
		logger::info("settings page registered with SKSE Menu Framework {:.2f}", SKSEMenuFramework::GetMenuFrameworkVersion());
	}

	bool IsCapturingKey() { return g_capturing.load(); }

	bool IsMenuOpen() { return g_registered && SKSEMenuFramework::IsAnyBlockingWindowOpened(); }
}
