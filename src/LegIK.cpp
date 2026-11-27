#include "LegIK.h"

namespace OAA::LegIK
{
	namespace
	{
		const RE::BSFixedString kNpc{ "NPC" };

		struct LegNames
		{
			RE::BSFixedString thigh, calf, foot;
		};
		const std::array<LegNames, 2> kLegs{ LegNames{ "NPC L Thigh [LThg]", "NPC L Calf [LClf]", "NPC L Foot [Lft ]" },
			LegNames{ "NPC R Thigh [RThg]", "NPC R Calf [RClf]", "NPC R Foot [Rft ]" } };

		float Len(const RE::NiPoint3& v) { return std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z); }

		RE::NiPoint3 Norm(const RE::NiPoint3& v)
		{
			const float l = Len(v);
			return l > 1e-6f ? v / l : RE::NiPoint3{};
		}

		// rotation that turns direction a_from into direction a_to (Rodrigues)
		RE::NiMatrix3 RotBetween(const RE::NiPoint3& a_from, const RE::NiPoint3& a_to)
		{
			const auto f = Norm(a_from);
			const auto t = Norm(a_to);
			auto axis = f.Cross(t);
			const float s = Len(axis);
			const float c = std::clamp(f.Dot(t), -1.0f, 1.0f);
			RE::NiMatrix3 r;  // identity
			if (s < 1e-6f) {
				return r;
			}
			axis = axis / s;
			const float k[3]{ axis.x, axis.y, axis.z };
			const float K[3][3]{ { 0, -k[2], k[1] }, { k[2], 0, -k[0] }, { -k[1], k[0], 0 } };
			for (int i = 0; i < 3; ++i) {
				for (int j = 0; j < 3; ++j) {
					r.entry[i][j] = (i == j ? c : 0.0f) + (1.0f - c) * k[i] * k[j] + s * K[i][j];
				}
			}
			return r;
		}

		bool IsAncestor(RE::NiAVObject* a_ancestor, RE::NiAVObject* a_node)
		{
			for (RE::NiAVObject* n = a_node ? a_node->parent : nullptr; n; n = n->parent) {
				if (n == a_ancestor) {
					return true;
				}
			}
			return false;
		}

		// give a_node the world rotation a_worldRot (keeping its position), then refresh its subtree
		void SetWorldRotation(RE::NiAVObject* a_node, const RE::NiMatrix3& a_worldRot)
		{
			a_node->local.rotate = a_node->parent->world.rotate.Transpose() * a_worldRot;
			RE::NiUpdateData ud{};
			a_node->Update(ud);
		}

		std::string Chain(RE::NiAVObject* a_node)
		{
			std::string s;
			int i = 0;
			for (RE::NiAVObject* n = a_node; n && i < 6; n = n->parent, ++i) {
				s += std::format("{}'{}'", i ? " <- " : "", n->name.c_str());
			}
			return s;
		}
	}

	Result Apply(RE::NiAVObject* a_root, const RE::NiPoint3& a_offsetLocal, Mode a_mode, std::string* a_failInfo)
	{
		Result result;
		if (!a_root) {
			return result;
		}
		const auto npc = a_root->GetObjectByName(kNpc);
		if (!npc || !npc->parent) {
			return result;
		}
		// world transforms are current: the caller refreshed the NPC subtree after moving it
		const auto& parentW = npc->parent->world;
		const auto offsetWorld = (parentW.rotate * a_offsetLocal) * parentW.scale;
		const auto forward = a_root->world.rotate * RE::NiPoint3{ 0.0f, 1.0f, 0.0f };

		for (size_t leg = 0; leg < kLegs.size(); ++leg) {
			const auto thigh = a_root->GetObjectByName(kLegs[leg].thigh);
			const auto calf = a_root->GetObjectByName(kLegs[leg].calf);
			const auto foot = a_root->GetObjectByName(kLegs[leg].foot);
			if (!thigh || !calf || !foot || !thigh->parent || !calf->parent || !foot->parent || !IsAncestor(thigh, calf) || !IsAncestor(calf, foot)) {
				if (a_failInfo && a_failInfo->empty()) {
					*a_failInfo = std::format("thigh={} calf={} foot={}; foot chain: {}", thigh != nullptr, calf != nullptr, foot != nullptr,
						foot ? Chain(foot) : "-"s);
				}
				continue;
			}

			const auto H = thigh->world.translate;
			const auto K = calf->world.translate;
			const auto F = foot->world.translate;
			const auto footRot = foot->world.rotate;
			const auto T = F - offsetWorld;  // where the animation had the foot before we moved the body

			const float a = Len(K - H);
			const float b = Len(F - K);
			if (a < 1.0f || b < 1.0f) {
				continue;
			}

			if (a_mode == Mode::kKneel) {
				const auto K0 = K - offsetWorld;  // knee stays on the floor
				SetWorldRotation(thigh, RotBetween(K - H, K0 - H) * thigh->world.rotate);
				const auto K2 = calf->world.translate;
				SetWorldRotation(calf, RotBetween(foot->world.translate - K2, T - K2) * calf->world.rotate);
				SetWorldRotation(foot, footRot);
				if (leg == 0) {
					result.footError = Len(calf->world.translate - K0);  // how far the knee left the floor spot
					result.calf = calf;
					result.calfLocal = calf->local.rotate;
				}
				result.applied = true;
				continue;
			}
			const auto HT = T - H;
			const float dRaw = Len(HT);
			if (dRaw < 1e-3f) {
				continue;
			}
			const float d = std::clamp(dRaw, std::fabs(a - b) + 0.01f, a + b - 0.01f);
			const auto u = HT / dRaw;

			// bend plane: keep the knee pointing where the animation points it
			auto p = (K - H) - u * (K - H).Dot(u);
			if (Len(p) < 0.05f * a) {
				p = forward - u * forward.Dot(u);
			}
			p = Norm(p);

			const float cosA = std::clamp((a * a + d * d - b * b) / (2.0f * a * d), -1.0f, 1.0f);
			const float sinA = std::sqrt(std::max(0.0f, 1.0f - cosA * cosA));
			const auto newK = H + (u * cosA + p * sinA) * a;

			// thigh: swing the hip->knee direction onto the new knee
			SetWorldRotation(thigh, RotBetween(K - H, newK - H) * thigh->world.rotate);

			// calf: swing the knee->foot direction onto the planted foot target
			const auto K2 = calf->world.translate;
			SetWorldRotation(calf, RotBetween(foot->world.translate - K2, T - K2) * calf->world.rotate);

			// foot keeps its animated world orientation (flat on the floor)
			SetWorldRotation(foot, footRot);

			if (leg == 0) {
				result.footError = Len(foot->world.translate - T);
				result.calf = calf;
				result.calfLocal = calf->local.rotate;
			}
			result.applied = true;
		}
		return result;
	}

	bool IsAncestorOf(RE::NiAVObject* a_ancestor, RE::NiAVObject* a_node)
	{
		return IsAncestor(a_ancestor, a_node);
	}

	float Reach(RE::NiAVObject* a_upper, RE::NiAVObject* a_lower, RE::NiAVObject* a_end, const RE::NiPoint3& a_target,
		const RE::NiPoint3& a_fallbackPole, float a_endFollow, float a_maxEndAngle, float a_swivel, float* a_wristAngle)
	{
		const auto endLocal = a_end->local.rotate;  // animated wrist relative to the forearm
		const auto S = a_upper->world.translate;
		const auto E = a_lower->world.translate;
		const auto W = a_end->world.translate;
		const auto endRot = a_end->world.rotate;

		const float a = Len(E - S);
		const float b = Len(W - E);
		const auto ST = a_target - S;
		const float dRaw = Len(ST);
		if (a < 0.5f || b < 0.5f || dRaw < 1e-3f) {
			return Len(W - a_target);
		}
		const float d = std::clamp(dRaw, std::fabs(a - b) + 0.01f, a + b - 0.01f);
		const auto u = ST / dRaw;

		auto p = (E - S) - u * (E - S).Dot(u);
		if (Len(p) < 0.05f * a) {
			p = a_fallbackPole - u * a_fallbackPole.Dot(u);
		}
		p = Norm(p);
		if (a_swivel != 0.0f) {
			// swing the elbow around the shoulder->target axis
			p = p * std::cos(a_swivel) + u.Cross(p) * std::sin(a_swivel);
		}

		const float cosA = std::clamp((a * a + d * d - b * b) / (2.0f * a * d), -1.0f, 1.0f);
		const float sinA = std::sqrt(std::max(0.0f, 1.0f - cosA * cosA));
		const auto newE = S + (u * cosA + p * sinA) * a;

		SetWorldRotation(a_upper, RotBetween(E - S, newE - S) * a_upper->world.rotate);
		const auto E2 = a_lower->world.translate;
		SetWorldRotation(a_lower, RotBetween(a_end->world.translate - E2, a_target - E2) * a_lower->world.rotate);

		// end orientation: start from "follows the lower bone" (animated wrist bend), turn toward
		// "keeps the animated world orientation" by (1 - follow), but never more than a_maxEndAngle
		const auto follow = a_lower->world.rotate * endLocal;
		const auto D = follow.Transpose() * endRot;
		const float tr = D.entry[0][0] + D.entry[1][1] + D.entry[2][2];
		const float theta = std::acos(std::clamp((tr - 1.0f) * 0.5f, -1.0f, 1.0f));
		if (a_wristAngle) {
			*a_wristAngle = theta;  // wrist bend needed to keep the animated hand orientation
		}
		RE::NiMatrix3 result = follow;
		if (theta > 1e-4f && theta < 3.1f) {
			RE::NiPoint3 axis{ D.entry[2][1] - D.entry[1][2], D.entry[0][2] - D.entry[2][0], D.entry[1][0] - D.entry[0][1] };
			axis = axis / (2.0f * std::sin(theta));
			const float ang = std::min((1.0f - std::clamp(a_endFollow, 0.0f, 1.0f)) * theta, a_maxEndAngle);
			const float c = std::cos(ang), sn = std::sin(ang);
			const float k[3]{ axis.x, axis.y, axis.z };
			const float K[3][3]{ { 0, -k[2], k[1] }, { k[2], 0, -k[0] }, { -k[1], k[0], 0 } };
			RE::NiMatrix3 r;
			for (int i = 0; i < 3; ++i) {
				for (int j = 0; j < 3; ++j) {
					r.entry[i][j] = (i == j ? c : 0.0f) + (1.0f - c) * k[i] * k[j] + sn * K[i][j];
				}
			}
			result = follow * r;
		} else if (theta <= 1e-4f) {
			result = endRot;
		}
		SetWorldRotation(a_end, result);
		return Len(a_end->world.translate - a_target);
	}

	float SwingToward(RE::NiAVObject* a_node, const RE::NiPoint3& a_from, const RE::NiPoint3& a_to, float a_fraction, float a_maxAngle)
	{
		const auto P = a_node->world.translate;
		const auto f = Norm(a_from - P);
		const auto t = Norm(a_to - P);
		auto axis = f.Cross(t);
		const float s = Len(axis);
		if (s < 1e-5f) {
			return 0.0f;
		}
		axis = axis / s;
		const float full = std::atan2(s, f.Dot(t));
		const float ang = std::min(full * std::clamp(a_fraction, 0.0f, 1.0f), a_maxAngle);
		const float c = std::cos(ang), sn = std::sin(ang);
		const float k[3]{ axis.x, axis.y, axis.z };
		const float K[3][3]{ { 0, -k[2], k[1] }, { k[2], 0, -k[0] }, { -k[1], k[0], 0 } };
		RE::NiMatrix3 r;
		for (int i = 0; i < 3; ++i) {
			for (int j = 0; j < 3; ++j) {
				r.entry[i][j] = (i == j ? c : 0.0f) + (1.0f - c) * k[i] * k[j] + sn * K[i][j];
			}
		}
		SetWorldRotation(a_node, r * a_node->world.rotate);
		return ang;
	}
}
