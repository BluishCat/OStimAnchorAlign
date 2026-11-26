#include "Settings.h"

namespace OAA
{
	namespace
	{
		constexpr auto kIniPath = L"Data\\SKSE\\Plugins\\OStimAnchorAlign.ini";

		std::wstring ReadRaw(const wchar_t* a_section, const wchar_t* a_key)
		{
			wchar_t buf[128]{};
			::GetPrivateProfileStringW(a_section, a_key, L"", buf, static_cast<DWORD>(std::size(buf)), kIniPath);
			std::wstring s = buf;
			// strip trailing inline comments and whitespace
			if (auto p = s.find_first_of(L";#"); p != std::wstring::npos) {
				s.resize(p);
			}
			while (!s.empty() && iswspace(s.back())) {
				s.pop_back();
			}
			return s;
		}

		float ReadFloat(const wchar_t* a_section, const wchar_t* a_key, float a_default)
		{
			const auto s = ReadRaw(a_section, a_key);
			if (s.empty()) {
				return a_default;
			}
			wchar_t* end = nullptr;
			const float v = std::wcstof(s.c_str(), &end);
			return end == s.c_str() ? a_default : v;
		}

		int ReadInt(const wchar_t* a_section, const wchar_t* a_key, int a_default)
		{
			const auto s = ReadRaw(a_section, a_key);
			if (s.empty()) {
				return a_default;
			}
			wchar_t* end = nullptr;
			const long v = std::wcstol(s.c_str(), &end, 0);
			return end == s.c_str() ? a_default : static_cast<int>(v);
		}
	}

	Settings Settings::Load()
	{
		Settings s;
		s.enabled = ReadInt(L"General", L"bEnabled", 1) != 0;
		s.mode = static_cast<Mode>(std::clamp(ReadInt(L"General", L"iMode", 1), 0, 1));
		s.settleSeconds = std::clamp(ReadFloat(L"General", L"fSettleSeconds", s.settleSeconds), 0.0f, 10.0f);
		s.sampleSeconds = std::clamp(ReadFloat(L"General", L"fSampleSeconds", s.sampleSeconds), 0.1f, 30.0f);
		s.smoothSeconds = std::clamp(ReadFloat(L"General", L"fSmoothSeconds", s.smoothSeconds), 0.0f, 10.0f);
		s.moveMode = static_cast<MoveMode>(std::clamp(ReadInt(L"General", L"iMoveMode", 0), 0, 3));
		s.strength = std::clamp(ReadFloat(L"General", L"fStrength", s.strength), 0.0f, 2.0f);
		s.horizontalScale = std::clamp(ReadFloat(L"General", L"fHorizontalScale", s.horizontalScale), 0.0f, 2.0f);
		s.verticalScale = std::clamp(ReadFloat(L"General", L"fVerticalScale", s.verticalScale), 0.0f, 2.0f);
		s.maxOffset = std::clamp(ReadFloat(L"General", L"fMaxOffset", s.maxOffset), 0.0f, 500.0f);
		s.minHeightDiff = std::clamp(ReadFloat(L"General", L"fMinHeightDiff", s.minHeightDiff), 0.0f, 1.0f);
		s.groundedHeight = std::clamp(ReadFloat(L"General", L"fGroundedHeight", s.groundedHeight), 0.0f, 200.0f);
		s.groundedZMobility = std::clamp(ReadFloat(L"General", L"fGroundedZMobility", s.groundedZMobility), 0.0f, 1.0f);
		s.legIK = ReadInt(L"LegIK", L"bEnabled", 1) != 0;
		s.maxLegDrop = std::clamp(ReadFloat(L"LegIK", L"fMaxDrop", s.maxLegDrop), 0.0f, 100.0f);
		s.kneelLift = ReadInt(L"LegIK", L"bKneelLift", 1) != 0;
		s.kneelRiseShare = std::clamp(ReadFloat(L"LegIK", L"fKneelRiseShare", s.kneelRiseShare), 0.05f, 0.95f);
		s.kneelHeight = std::clamp(ReadFloat(L"LegIK", L"fKneelHeight", s.kneelHeight), 0.0f, 100.0f);
		s.kneelMinHipHeight = std::clamp(ReadFloat(L"LegIK", L"fKneelMinHipHeight", s.kneelMinHipHeight), 0.0f, 200.0f);
		s.maxKneelRise = std::clamp(ReadFloat(L"LegIK", L"fMaxKneelRise", s.maxKneelRise), 0.0f, 100.0f);
		s.lyingHeight = std::clamp(ReadFloat(L"General", L"fLyingHeight", s.lyingHeight), 0.0f, 200.0f);
		s.kneelTorsoUp = std::clamp(ReadFloat(L"LegIK", L"fKneelTorsoUp", s.kneelTorsoUp), 0.0f, 100.0f);
		s.verbose = ReadInt(L"Debug", L"bVerboseLog", 0) != 0;
		return s;
	}
}
