#include "OStimBridge.h"

#include "Aligner.h"

#include "OStim/PluginInterface/InterfaceExchangeMessage.h"
#include "OStim/PluginInterface/Threading/ThreadInterface.h"

namespace OAA::Bridge
{
	namespace
	{
		SceneSnapshot Snapshot(OStim::Thread* a_thread)
		{
			SceneSnapshot snap;
			snap.threadID = a_thread->getThreadID();

			const uint32_t actorCount = a_thread->getActorCount();
			for (uint32_t i = 0; i < actorCount; ++i) {
				RE::ActorHandle handle;
				if (auto ta = a_thread->getActor(i)) {
					if (auto actor = static_cast<RE::Actor*>(ta->getGameActor())) {
						handle = actor->GetHandle();
					}
				}
				snap.actors.push_back(handle);
			}

			if (auto node = a_thread->getCurrentNode()) {
				if (auto id = node->getNodeID()) {
					snap.nodeID = id;
				}
				const uint32_t actionCount = node->getActionCount();
				for (uint32_t i = 0; i < actionCount; ++i) {
					auto action = node->getAction(i);
					if (!action) {
						continue;
					}
					auto type = action->getType();
					if (!type || !type->getActionID()) {
						continue;
					}
					SceneSnapshot::RawAction raw;
					raw.id = type->getActionID();
					std::ranges::transform(raw.id, raw.id.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
					raw.actor = action->getActor(OStim::Role::ACTOR);
					raw.target = action->getActor(OStim::Role::TARGET);
					snap.actions.push_back(std::move(raw));
				}
			}
			return snap;
		}

		class NodeChangedListener : public OStim::ThreadEventListener
		{
		public:
			void listen(OStim::Thread* a_thread) override
			{
				if (!a_thread) {
					return;
				}
				try {
					Aligner::Get().OnNodeChanged(Snapshot(a_thread));
				} catch (const std::exception& e) {
					logger::error("node changed listener: {}", e.what());
				}
			}
		};

		class ThreadStopListener : public OStim::ThreadEventListener
		{
		public:
			void listen(OStim::Thread* a_thread) override
			{
				if (a_thread) {
					Aligner::Get().OnThreadStop(a_thread->getThreadID());
				}
			}
		};

		OStim::ThreadInterface* g_threads = nullptr;

		NodeChangedListener g_nodeChanged;
		ThreadStopListener g_threadStop;

		OStim::InterfaceMap* GetInterfaceMap()
		{
			// OStim 7.5.1c+ exports requestInterfaceMap; older 7.5.x answer an SKSE message instead.
			if (auto map = OStim::getInferfaceMap()) {
				return map;
			}
			OStim::InterfaceExchangeMessage msg;
			if (auto messaging = SKSE::GetMessagingInterface()) {
				messaging->Dispatch(OStim::InterfaceExchangeMessage::MESSAGE_TYPE, &msg, sizeof(msg), "OStim");
			}
			return msg.interfaceMap;
		}
	}

	bool Connect()
	{
		if (!GetModuleHandleA("OStim.dll")) {
			logger::error("OStim.dll is not loaded; nothing to do");
			return false;
		}
		auto map = GetInterfaceMap();
		if (!map) {
			logger::error("could not get OStim's interface map (OStim Standalone 7.5 or newer is required)");
			return false;
		}
		auto threads = static_cast<OStim::ThreadInterface*>(map->queryInterface(OStim::ThreadInterface::NAME));
		if (!threads) {
			logger::error("OStim has no '{}' interface", OStim::ThreadInterface::NAME);
			return false;
		}
		logger::info("OStim thread interface version {:08X}", threads->getVersion());

		g_threads = threads;
		threads->registerNodeChangedListener(&g_nodeChanged);
		threads->registerThreadStopListener(&g_threadStop);
		logger::info("listening to OStim node changes");
		return true;
	}

	int32_t StartScene(std::vector<RE::Actor*> a_actors)
	{
		if (!g_threads || g_threads->getVersion() < 2) {
			logger::error("OStim's thread interface can't start scenes (version too old)");
			return -1;
		}
		std::vector<void*> actors(a_actors.begin(), a_actors.end());
		auto builder = g_threads->createThreadBuilder(static_cast<uint32_t>(actors.size()), actors.data());
		if (!builder) {
			return -1;
		}
		return builder->start();
	}

	bool IsThreadRunning(int32_t a_threadID)
	{
		return g_threads && a_threadID >= 0 && g_threads->getThread(a_threadID) != nullptr;
	}
}
