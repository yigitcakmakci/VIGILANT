#include "AI/GoalManager.hpp"
#include "Data/DatabaseManager.hpp"
#include <algorithm>
#include <windows.h>

#define WM_WEBVIEW_ACTIVEAPP (WM_APP + 3)
extern DWORD g_WebViewThreadId;

static void DebugLog(const std::string& msg) {
	std::wstring wmsg(msg.begin(), msg.end());
	OutputDebugStringW((L"[GoalManager] " + wmsg + L"\n").c_str());
}

GoalManager::GoalManager(DatabaseManager* vault, EventBridge* bridge)
	: m_vault(vault), m_bridge(bridge)
{
}

void GoalManager::addGoal(const nlohmann::json& dynamicGoalTree) {
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		m_goals.push_back(dynamicGoalTree);
		DebugLog("Goal added. Total goals: " + std::to_string(m_goals.size()));
	}
	broadcastSummary();
}

bool GoalManager::removeGoal(const std::string& rootNodeId) {
	{
		std::lock_guard<std::mutex> lock(m_mutex);

		auto it = std::remove_if(m_goals.begin(), m_goals.end(),
			[&rootNodeId](const nlohmann::json& tree) {
				if (tree.contains("root") && tree["root"].contains("id")) {
					return tree["root"]["id"].get<std::string>() == rootNodeId;
				}
				if (tree.contains("id")) {
					return tree["id"].get<std::string>() == rootNodeId;
				}
				return false;
			});

		if (it != m_goals.end()) {
			m_goals.erase(it, m_goals.end());
			DebugLog("Goal removed: " + rootNodeId + ". Remaining: " + std::to_string(m_goals.size()));
		} else {
			DebugLog("Goal not found for removal: " + rootNodeId);
			return false;
		}
	}
	broadcastSummary();
	return true;
}

nlohmann::json GoalManager::getAllGoals() const {
	std::lock_guard<std::mutex> lock(m_mutex);
	nlohmann::json arr = nlohmann::json::array();
	for (const auto& g : m_goals) {
		arr.push_back(g);
	}
	return arr;
}

nlohmann::json GoalManager::getGoalById(const std::string& rootNodeId) const {
	std::lock_guard<std::mutex> lock(m_mutex);
	for (const auto& tree : m_goals) {
		if (tree.contains("root") && tree["root"].contains("id") &&
			tree["root"]["id"].get<std::string>() == rootNodeId) {
			return tree;
		}
		if (tree.contains("id") && tree["id"].get<std::string>() == rootNodeId) {
			return tree;
		}
	}
	return nlohmann::json();
}

void GoalManager::loadFromStorage() {
	// Goals are primarily managed on the frontend via localStorage.
	// This method can be extended later to load from DatabaseManager.
	std::lock_guard<std::mutex> lock(m_mutex);
	DebugLog("loadFromStorage: Currently " + std::to_string(m_goals.size()) + " goals in memory");
}

void GoalManager::saveToStorage() const {
	// Goals are primarily persisted on the frontend via localStorage.
	// This method can be extended later to save to DatabaseManager.
	std::lock_guard<std::mutex> lock(m_mutex);
	DebugLog("saveToStorage: " + std::to_string(m_goals.size()) + " goals");
}

size_t GoalManager::count() const {
	std::lock_guard<std::mutex> lock(m_mutex);
	return m_goals.size();
}

// ── Active goal tracking ───────────────────────────────────────────────

void GoalManager::setActiveGoal(const std::string& goalId) {
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		m_lastActiveGoalId = goalId;
		DebugLog("Active goal set: " + goalId);
	}
	broadcastSummary();
}

// ── Dashboard summary ──────────────────────────────────────────────────

nlohmann::json GoalManager::summarizeGoal(const nlohmann::json& tree, bool includeChildren) {
	nlohmann::json summary;

	// DynamicGoalTree envelope: { root: { id, title, progress, children, actionItems } }
	if (tree.contains("root") && tree["root"].is_object()) {
		const auto& root = tree["root"];
		summary["id"]       = root.value("id", "");
		summary["title"]    = root.value("title", "");
		summary["progress"] = root.value("progress", 0);

		if (includeChildren) {
			if (root.contains("children") && root["children"].is_array())
				summary["children"] = root["children"];
			if (root.contains("actionItems") && root["actionItems"].is_array())
				summary["actionItems"] = root["actionItems"];
		}
	}
	// Raw GoalNode: { id, title, progress, ... }
	else if (tree.contains("id")) {
		summary["id"]       = tree.value("id", "");
		summary["title"]    = tree.value("title", "");
		summary["progress"] = tree.value("progress", 0);

		if (includeChildren) {
			if (tree.contains("children") && tree["children"].is_array())
				summary["children"] = tree["children"];
			if (tree.contains("actionItems") && tree["actionItems"].is_array())
				summary["actionItems"] = tree["actionItems"];
		}
	}

	return summary;
}

nlohmann::json GoalManager::getDashboardSummary() const {
	std::lock_guard<std::mutex> lock(m_mutex);

	nlohmann::json result;
	result["type"]    = "GoalTreeSummaryUpdated";
	result["version"] = 1;

	nlohmann::json goals = nlohmann::json::array();
	for (const auto& tree : m_goals) {
		// Determine the root id to check against lastActiveGoalId
		std::string rootId;
		if (tree.contains("root") && tree["root"].contains("id"))
			rootId = tree["root"]["id"].get<std::string>();
		else if (tree.contains("id"))
			rootId = tree["id"].get<std::string>();

		bool isActive = (!m_lastActiveGoalId.empty() && rootId == m_lastActiveGoalId);
		goals.push_back(summarizeGoal(tree, isActive));
	}

	result["payload"] = {
		{"lastActiveGoalId", m_lastActiveGoalId},
		{"goals", goals}
	};

	return result;
}

// ── Broadcast via EventBridge ──────────────────────────────────────────

void GoalManager::broadcastSummary() {
	try {
		nlohmann::json summary = getDashboardSummary();
		std::string jsonStr = summary.dump();

		// Post directly to the WebView thread using the same mechanism as EventBridge::SendToWebView.
		DWORD threadId = g_WebViewThreadId;
		if (threadId == 0) {
			DebugLog("broadcastSummary: WebView thread not ready");
			return;
		}

		std::string* pJson = new std::string(jsonStr);
		if (!PostThreadMessageW(threadId, WM_WEBVIEW_ACTIVEAPP, 0, reinterpret_cast<LPARAM>(pJson))) {
			delete pJson;
			DebugLog("broadcastSummary: PostThreadMessage failed");
			return;
		}

		DebugLog("Dashboard summary broadcast (" + std::to_string(summary["payload"]["goals"].size()) + " goals)");
	}
	catch (const std::exception& e) {
		DebugLog(std::string("broadcastSummary error: ") + e.what());
	}
}
