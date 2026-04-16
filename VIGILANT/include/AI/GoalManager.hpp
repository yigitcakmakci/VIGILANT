#ifndef GOAL_MANAGER_HPP
#define GOAL_MANAGER_HPP

#include <string>
#include <vector>
#include <mutex>
#include "AI/GoalTree.hpp"
#include "Utils/json.hpp"

class DatabaseManager;
class EventBridge;

// ═══════════════════════════════════════════════════════════════════════
// GoalManager — manages a collection of goal trees (GoalForest).
//
// Instead of a single root GoalNode, the system now maintains a vector
// of independent goal trees.  New interview completions append to the
// forest rather than replacing the existing tree.
// ═══════════════════════════════════════════════════════════════════════

class GoalManager {
public:
	explicit GoalManager(DatabaseManager* vault, EventBridge* bridge = nullptr);

	/// Add a new goal tree (DynamicGoalTree JSON envelope) to the forest.
	void addGoal(const nlohmann::json& dynamicGoalTree);

	/// Remove a goal tree whose root node id matches the given id.
	/// Returns true if found and removed.
	bool removeGoal(const std::string& rootNodeId);

	/// Get all goal trees as a JSON array.
	nlohmann::json getAllGoals() const;

	/// Get a single goal tree by root node id. Returns null JSON if not found.
	nlohmann::json getGoalById(const std::string& rootNodeId) const;

	/// Load all goals from persistent storage.
	void loadFromStorage();

	/// Persist all goals to storage.
	void saveToStorage() const;

	/// Return the number of goals in the forest.
	size_t count() const;

	/// Set the last active goal id (called when user opens/interacts with a goal).
	void setActiveGoal(const std::string& goalId);

	/// Build an optimized dashboard summary JSON.
	/// Contains {id, title, progress} for all goals, plus children/actionItems
	/// for the last-active goal.
	nlohmann::json getDashboardSummary() const;

	/// Broadcast the dashboard summary via EventBridge (GoalTreeSummaryUpdated).
	void broadcastSummary();

private:
	/// Helper to extract {id, title, progress} from a goal tree JSON.
	static nlohmann::json summarizeGoal(const nlohmann::json& tree, bool includeChildren);

	DatabaseManager*              m_vault;
	EventBridge*                  m_bridge;
	std::vector<nlohmann::json>   m_goals;   // each element is a DynamicGoalTree JSON
	std::string                   m_lastActiveGoalId;
	mutable std::mutex            m_mutex;
};

#endif // GOAL_MANAGER_HPP
