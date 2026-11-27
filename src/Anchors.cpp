#include "Anchors.h"

namespace OAA
{
	namespace
	{
		constexpr auto kJsonPath = "Data/SKSE/Plugins/OStimAnchorAlign/anchors.json";

		std::string Lower(std::string s)
		{
			std::ranges::transform(s, s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
			return s;
		}

		Anchors Defaults()
		{
			Anchors a;
			a.bones = {
				{ "pelvis", { "NPC Pelvis [Pelv]" } },
				{ "penis", { "NPC Genitals01 [Gen01]", "NPC Pelvis [Pelv]" } },
				{ "vagina", { "VaginaDeep1", "NPC Pelvis [Pelv]" } },
				{ "anus", { "NPC Pelvis [Pelv]" } },
				{ "head", { "NPC Head [Head]" } },
				{ "mouth", { "NPC Head [Head]" } },
				{ "neck", { "NPC Neck [Neck]" } },
				{ "chest", { "NPC Spine2 [Spn2]" } },
				{ "hand", { "NPC R Hand [RHnd]" } },
				{ "foot", { "NPC L Foot [Lft ]" } },
				{ "thigh", { "NPC L Thigh [LThg]" } },
			};
			auto add = [&](std::initializer_list<const char*> ids, const char* actor, const char* target, float w) {
				for (auto id : ids) {
					a.actions[id] = ActionRule{ actor, target, w };
				}
			};
			add({ "vaginalsex" }, "penis", "vagina", 1.0f);
			add({ "analsex" }, "penis", "anus", 1.0f);
			add({ "blowjob", "deepthroating", "penilelicking", "testicularlicking", "rubbingpenisagainstface" }, "mouth", "penis", 1.0f);
			add({ "vulvaleating", "vulvallicking" }, "mouth", "vagina", 1.0f);
			add({ "anallicking", "rimjob" }, "mouth", "anus", 1.0f);
			add({ "kissing", "frenchkissing" }, "mouth", "mouth", 1.0f);
			add({ "kissingcheek" }, "mouth", "head", 0.7f);
			add({ "kissingneck", "vampirebiting" }, "mouth", "neck", 0.7f);
			add({ "suckingnipple", "lickingnipple" }, "mouth", "chest", 0.7f);
			add({ "breastsmothering" }, "chest", "head", 0.6f);
			add({ "buttsmothering" }, "pelvis", "head", 0.6f);
			add({ "boobjob" }, "chest", "penis", 0.8f);
			add({ "buttjob", "grindingpenis" }, "pelvis", "penis", 0.8f);
			add({ "thighjob" }, "thigh", "penis", 0.6f);
			add({ "footjob" }, "foot", "penis", 0.5f);
			add({ "tribbing" }, "pelvis", "pelvis", 1.0f);
			add({ "grindingthigh" }, "pelvis", "thigh", 0.5f);
			add({ "hugging", "cuddling", "spooning" }, "chest", "chest", 0.4f);

			const std::string pelvis = "NPC Pelvis [Pelv]";
			a.handTargets = {
				{ "breast", { { "L Breast02", "R Breast02" }, { "NPC L Breast", "NPC R Breast" }, { "NPC Spine2 [Spn2]" } } },
				{ "butt", { { "NPC L Butt", "NPC R Butt" }, { pelvis } } },
				{ "vulva", { { "Clitoral1", "VaginaB1" }, { pelvis } } },
				{ "anus", { { pelvis } } },
				{ "penis", { { "NPC Genitals03 [Gen03]", "NPC Genitals02 [Gen02]" }, { pelvis } } },
				{ "testicles", { { "NPC GenitalsScrotum [GenScrot]" }, { pelvis } } },
				{ "head", { { "NPC Head [Head]" } } },
				{ "neck", { { "NPC Neck [Neck]" } } },
				{ "hip", { { pelvis } } },
				{ "thigh", { { "NPC L FrontThigh", "NPC R FrontThigh" }, { "NPC L Thigh [LThg]", "NPC R Thigh [RThg]" } } },
				{ "hand", { { "NPC L Hand [LHnd]", "NPC R Hand [RHnd]" } } },
				{ "arm", { { "NPC L Forearm [LLar]", "NPC R Forearm [RLar]" } } },
				{ "foot", { { "NPC L Foot [Lft ]", "NPC R Foot [Rft ]" } } },
				{ "body", { { "NPC Spine1 [Spn1]", "NPC Spine2 [Spn2]" } } },
			};
			auto hand = [&](std::initializer_list<const char*> ids, const char* target) {
				for (auto id : ids) {
					a.handActions[id] = target;
				}
			};
			hand({ "gropingbreast", "breastslapping" }, "breast");
			hand({ "gropingbutt" }, "butt");
			hand({ "gropingtesticles" }, "testicles");
			hand({ "handjob" }, "penis");
			hand({ "vaginalfingering", "vaginalfisting", "vulvalrubbing" }, "vulva");
			hand({ "analfingering", "analfisting" }, "anus");
			hand({ "pattinghead", "strokinghead", "holdinghead", "holdingchin" }, "head");
			hand({ "holdingneck" }, "neck");
			hand({ "holdinghip" }, "hip");
			hand({ "holdingthigh", "holdingleg" }, "thigh");
			hand({ "holdinghand" }, "hand");
			hand({ "holdingarm" }, "arm");
			hand({ "holdingfoot" }, "foot");
			hand({ "holdingbody" }, "body");
			return a;
		}
	}

	Anchors Anchors::Load()
	{
		Anchors a = Defaults();

		std::ifstream in(kJsonPath);
		if (!in) {
			logger::info("anchors.json not found, using built-in defaults ({} actions, {} hand actions)", a.actions.size(), a.handActions.size());
			return a;
		}

		try {
			const auto j = nlohmann::json::parse(in, nullptr, true, true);  // allow comments

			if (j.value("replaceDefaults", false)) {
				a.actions.clear();
				a.handActions.clear();
			}

			if (auto it = j.find("handTargets"); it != j.end() && it->is_object()) {
				for (auto& [name, groups] : it->items()) {
					std::vector<std::vector<std::string>> list;
					if (groups.is_array()) {
						for (auto& g : groups) {
							std::vector<std::string> group;
							if (g.is_string()) {
								group.push_back(g.get<std::string>());
							} else if (g.is_array()) {
								for (auto& n : g) {
									if (n.is_string()) {
										group.push_back(n.get<std::string>());
									}
								}
							}
							if (!group.empty()) {
								list.push_back(std::move(group));
							}
						}
					}
					if (!list.empty()) {
						a.handTargets[Lower(name)] = std::move(list);
					}
				}
			}

			if (auto it = j.find("handActions"); it != j.end() && it->is_object()) {
				for (auto& [id, target] : it->items()) {
					const auto key = Lower(id);
					if (target.is_string() && !target.get<std::string>().empty()) {
						a.handActions[key] = Lower(target.get<std::string>());
					} else {
						a.handActions.erase(key);
					}
				}
			}

			if (auto it = j.find("bones"); it != j.end() && it->is_object()) {
				for (auto& [name, list] : it->items()) {
					std::vector<std::string> names;
					if (list.is_string()) {
						names.push_back(list.get<std::string>());
					} else if (list.is_array()) {
						for (auto& n : list) {
							if (n.is_string()) {
								names.push_back(n.get<std::string>());
							}
						}
					}
					if (!names.empty()) {
						a.bones[Lower(name)] = std::move(names);
					}
				}
			}

			if (auto it = j.find("actions"); it != j.end() && it->is_object()) {
				for (auto& [id, rule] : it->items()) {
					const auto key = Lower(id);
					if (rule.is_null() || (rule.is_boolean() && !rule.get<bool>())) {
						a.actions.erase(key);  // "id": null / false disables a default rule
						continue;
					}
					if (!rule.is_object()) {
						continue;
					}
					ActionRule r;
					r.actorAnchor = Lower(rule.value("actor", ""s));
					r.targetAnchor = Lower(rule.value("target", ""s));
					r.weight = rule.value("weight", 1.0f);
					if (r.actorAnchor.empty() || r.targetAnchor.empty() || r.weight <= 0.0f) {
						a.actions.erase(key);
						continue;
					}
					a.actions[key] = std::move(r);
				}
			}
		} catch (const std::exception& e) {
			logger::error("anchors.json parse error: {} (using built-in defaults)", e.what());
			return Defaults();
		}

		// drop rules that reference unknown anchors
		std::erase_if(a.actions, [&](const auto& kv) {
			const bool ok = a.bones.contains(kv.second.actorAnchor) && a.bones.contains(kv.second.targetAnchor);
			if (!ok) {
				logger::warn("action '{}' references an unknown anchor, ignored", kv.first);
			}
			return !ok;
		});

		std::erase_if(a.handActions, [&](const auto& kv) {
			const bool ok = a.handTargets.contains(kv.second);
			if (!ok) {
				logger::warn("hand action '{}' references an unknown hand target, ignored", kv.first);
			}
			return !ok;
		});

		logger::info("anchors.json loaded: {} anchors, {} actions, {} hand targets, {} hand actions", a.bones.size(), a.actions.size(),
			a.handTargets.size(), a.handActions.size());
		return a;
	}
}
