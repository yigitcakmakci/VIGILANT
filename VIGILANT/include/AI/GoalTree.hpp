#ifndef GOAL_TREE_HPP
#define GOAL_TREE_HPP

#include <string>
#include <vector>
#include <set>
#include <map>
#include "Utils/json.hpp"

// ═══════════════════════════════════════════════════════════════════════
// GoalTree schema — deterministic output from Interview → AI pipeline.
//
// INVARIANT:
//   Every MicroTask.acceptance_criteria MUST be a non-empty string.
//   (Anti-hallucinated progress: no task can be marked "done" without
//    a concrete, verifiable criterion.)
//
// Mirrors the TypeScript types in ts/interview/goal-tree-types.ts.
// ═══════════════════════════════════════════════════════════════════════

// ── Evidence — proof attached when completing a MicroTask ──────────────
// Which field is required depends on MicroTask.evidence_type:
//   "text"   → text      must be non-empty
//   "file"   → file_path must be non-empty
//   "url"    → url       must be non-empty
//   "metric" → metric_value must be set (is_finite)

struct Evidence {
    std::string text;
    std::string file_path;
    std::string url;
    double      metric_value = 0.0;
    bool        has_metric   = false;   // sentinel: metric was explicitly set
};

// ── MicroTask ──────────────────────────────────────────────────────────

struct MicroTask {
    std::string              id;
    std::string              title;
    std::string              description;
    std::string              acceptance_criteria;   // REQUIRED, non-empty
    std::string              evidence_type;         // "text"|"file"|"url"|"metric"
    std::string              status;                // "open"|"done"
    std::vector<std::string> dependencies;
    Evidence                 evidence;              // optional while open, required when done
    bool                     has_evidence = false;  // sentinel: evidence block present
};

// ── MinorGoal ──────────────────────────────────────────────────────────

struct MinorGoal {
    std::string            id;
    std::string            title;
    std::string            description;
    std::vector<MicroTask> micros;
};

// ── MajorGoal ──────────────────────────────────────────────────────────

struct MajorGoal {
    std::string            id;
    std::string            title;
    std::string            description;
    std::vector<MinorGoal> minors;
};

// ── GoalTree (root) ────────────────────────────────────────────────────

struct GoalTree {
    int                    version = 1;
    std::string            session_id;
    std::string            generated_at;   // ISO-8601
    std::vector<MajorGoal> majors;
    // ── Versioning (replan support) ──
    std::string            version_id;      // unique id for this snapshot
    std::string            parent_version;  // version_id of parent (empty = first gen)
    std::string            created_ts;      // ISO-8601
};

// ═══════════════════════════════════════════════════════════════════════
// GoalTree Diff — tracks changes between two GoalTree versions
// ═══════════════════════════════════════════════════════════════════════

struct DiffEntry {
    std::string id;
    std::string type;       // "major" | "minor" | "micro"
    std::string change;     // "added" | "removed" | "changed"
    std::string title;
    std::vector<std::string> changedFields;
    // For orphaned micros
    std::string preservedStatus;
};

struct DiffSummary {
    int added    = 0;
    int removed  = 0;
    int changed  = 0;
    int orphaned = 0;
    int preserved = 0;
};

struct GoalTreeDiffResult {
    std::string old_version_id;
    std::string new_version_id;
    std::string timestamp;
    std::vector<DiffEntry> entries;
    DiffSummary summary;
};

// ═══════════════════════════════════════════════════════════════════════
// Validation result
// ═══════════════════════════════════════════════════════════════════════

struct GoalTreeValidation {
    bool        ok = false;
    std::string error;
    std::string path;      // JSON-path style location
};

// ── Structured tick-done error: {code, message, microTaskId} ──────────

struct TickValidationError {
    std::string code;
    std::string message;
    std::string microTaskId;
};

struct TickValidationResult {
    bool                           ok = false;
    std::vector<TickValidationError> errors;
};

// ═══════════════════════════════════════════════════════════════════════
// Validation
// ═══════════════════════════════════════════════════════════════════════

namespace GoalTreeSchema {

inline bool isValidEvidenceType(const std::string& v) {
    return v == "text" || v == "file" || v == "url" || v == "metric";
}

inline bool isValidStatus(const std::string& v) {
    return v == "open" || v == "done";
}

inline GoalTreeValidation fail(const std::string& error, const std::string& path) {
    return { false, error, path };
}

inline GoalTreeValidation validateMicroTask(const nlohmann::json& j,
                                            const std::string& path,
                                            std::set<std::string>& allMicroIds) {
    if (!j.is_object())
        return fail("MicroTask is not an object", path);

    // id
    if (!j.contains("id") || !j["id"].is_string() || j["id"].get<std::string>().empty())
        return fail("Missing or empty id", path + ".id");

    std::string id = j["id"].get<std::string>();
    if (allMicroIds.count(id))
        return fail("Duplicate micro id: \"" + id + "\"", path + ".id");
    allMicroIds.insert(id);

    // title
    if (!j.contains("title") || !j["title"].is_string() || j["title"].get<std::string>().empty())
        return fail("Missing or empty title", path + ".title");

    // description
    if (!j.contains("description") || !j["description"].is_string())
        return fail("Missing description", path + ".description");

    // ── CRITICAL: acceptance_criteria ──────────────────────────────────
    if (!j.contains("acceptance_criteria") || !j["acceptance_criteria"].is_string()
        || j["acceptance_criteria"].get<std::string>().empty())
        return fail("acceptance_criteria is REQUIRED and must be a non-empty string "
                     "(anti-hallucinated progress)", path + ".acceptance_criteria");

    // evidence_type
    if (!j.contains("evidence_type") || !j["evidence_type"].is_string()
        || !isValidEvidenceType(j["evidence_type"].get<std::string>()))
        return fail("Invalid evidence_type (expected text|file|url|metric)",
                     path + ".evidence_type");

    // status
    if (!j.contains("status") || !j["status"].is_string()
        || !isValidStatus(j["status"].get<std::string>()))
        return fail("Invalid status (expected open|done)", path + ".status");

    // dependencies
    if (!j.contains("dependencies") || !j["dependencies"].is_array())
        return fail("dependencies must be a string array", path + ".dependencies");

    for (size_t i = 0; i < j["dependencies"].size(); ++i) {
        if (!j["dependencies"][i].is_string())
            return fail("dependencies[" + std::to_string(i) + "] is not a string",
                         path + ".dependencies[" + std::to_string(i) + "]");
    }

    // ── evidence: if status == "done", evidence must satisfy evidence_type ─
    if (j["status"].get<std::string>() == "done") {
        if (!j.contains("evidence") || !j["evidence"].is_object())
            return fail("evidence is required when status is 'done'",
                         path + ".evidence");

        const auto& ev = j["evidence"];
        std::string et = j["evidence_type"].get<std::string>();
        if (et == "text") {
            if (!ev.contains("text") || !ev["text"].is_string() || ev["text"].get<std::string>().empty())
                return fail("evidence_type is 'text' but evidence.text is missing or empty",
                             path + ".evidence.text");
        } else if (et == "file") {
            if (!ev.contains("file_path") || !ev["file_path"].is_string() || ev["file_path"].get<std::string>().empty())
                return fail("evidence_type is 'file' but evidence.file_path is missing or empty",
                             path + ".evidence.file_path");
        } else if (et == "url") {
            if (!ev.contains("url") || !ev["url"].is_string() || ev["url"].get<std::string>().empty())
                return fail("evidence_type is 'url' but evidence.url is missing or empty",
                             path + ".evidence.url");
        } else if (et == "metric") {
            if (!ev.contains("metric_value") || !ev["metric_value"].is_number())
                return fail("evidence_type is 'metric' but evidence.metric_value is missing or not a number",
                             path + ".evidence.metric_value");
        }
    }

    return { true, "", "" };
}

inline GoalTreeValidation validateMinorGoal(const nlohmann::json& j,
                                            const std::string& path,
                                            std::set<std::string>& allMicroIds) {
    if (!j.is_object())
        return fail("MinorGoal is not an object", path);

    if (!j.contains("id") || !j["id"].is_string() || j["id"].get<std::string>().empty())
        return fail("Missing or empty id", path + ".id");
    if (!j.contains("title") || !j["title"].is_string() || j["title"].get<std::string>().empty())
        return fail("Missing or empty title", path + ".title");
    if (!j.contains("description") || !j["description"].is_string())
        return fail("Missing description", path + ".description");

    if (!j.contains("micros") || !j["micros"].is_array())
        return fail("micros must be an array", path + ".micros");
    if (j["micros"].empty())
        return fail("micros array must not be empty", path + ".micros");

    for (size_t i = 0; i < j["micros"].size(); ++i) {
        auto v = validateMicroTask(j["micros"][i],
                                   path + ".micros[" + std::to_string(i) + "]",
                                   allMicroIds);
        if (!v.ok) return v;
    }
    return { true, "", "" };
}

inline GoalTreeValidation validateMajorGoal(const nlohmann::json& j,
                                            const std::string& path,
                                            std::set<std::string>& allMicroIds) {
    if (!j.is_object())
        return fail("MajorGoal is not an object", path);

    if (!j.contains("id") || !j["id"].is_string() || j["id"].get<std::string>().empty())
        return fail("Missing or empty id", path + ".id");
    if (!j.contains("title") || !j["title"].is_string() || j["title"].get<std::string>().empty())
        return fail("Missing or empty title", path + ".title");
    if (!j.contains("description") || !j["description"].is_string())
        return fail("Missing description", path + ".description");

    if (!j.contains("minors") || !j["minors"].is_array())
        return fail("minors must be an array", path + ".minors");
    if (j["minors"].empty())
        return fail("minors array must not be empty", path + ".minors");

    for (size_t i = 0; i < j["minors"].size(); ++i) {
        auto v = validateMinorGoal(j["minors"][i],
                                   path + ".minors[" + std::to_string(i) + "]",
                                   allMicroIds);
        if (!v.ok) return v;
    }
    return { true, "", "" };
}

// ── Cross-reference: dependency IDs must point to existing micros ──────

inline GoalTreeValidation validateDependencyRefs(const nlohmann::json& root,
                                                  const std::set<std::string>& allMicroIds) {
    for (size_t mi = 0; mi < root["majors"].size(); ++mi) {
        const auto& major = root["majors"][mi];
        for (size_t ni = 0; ni < major["minors"].size(); ++ni) {
            const auto& minor = major["minors"][ni];
            for (size_t ti = 0; ti < minor["micros"].size(); ++ti) {
                const auto& micro = minor["micros"][ti];
                std::string microId = micro["id"].get<std::string>();
                for (size_t di = 0; di < micro["dependencies"].size(); ++di) {
                    std::string dep = micro["dependencies"][di].get<std::string>();
                    if (allMicroIds.find(dep) == allMicroIds.end()) {
                        return fail("Dependency \"" + dep + "\" references a non-existent MicroTask id",
                                     "$.majors[" + std::to_string(mi) + "].minors["
                                     + std::to_string(ni) + "].micros["
                                     + std::to_string(ti) + "].dependencies");
                    }
                    if (dep == microId) {
                        return fail("MicroTask \"" + microId + "\" depends on itself",
                                     "$.majors[" + std::to_string(mi) + "].minors["
                                     + std::to_string(ni) + "].micros["
                                     + std::to_string(ti) + "].dependencies");
                    }
                }
            }
        }
    }
    return { true, "", "" };
}

/**
 * Full GoalTree validation.  Call this on any JSON before accepting it
 * as a GoalTree document.
 */
inline GoalTreeValidation validate(const nlohmann::json& j) {
    if (!j.is_object())
        return fail("Expected a JSON object", "$");

    // version
    if (!j.contains("version") || !j["version"].is_number_integer()
        || j["version"].get<int>() != 1)
        return fail("Unsupported version (expected 1)", "$.version");

    // session_id
    if (!j.contains("session_id") || !j["session_id"].is_string()
        || j["session_id"].get<std::string>().empty())
        return fail("Missing or empty session_id", "$.session_id");

    // generated_at
    if (!j.contains("generated_at") || !j["generated_at"].is_string()
        || j["generated_at"].get<std::string>().empty())
        return fail("Missing or empty generated_at", "$.generated_at");

    // majors
    if (!j.contains("majors") || !j["majors"].is_array())
        return fail("majors must be an array", "$.majors");
    if (j["majors"].empty())
        return fail("majors array must not be empty", "$.majors");

    std::set<std::string> allMicroIds;

    for (size_t i = 0; i < j["majors"].size(); ++i) {
        auto v = validateMajorGoal(j["majors"][i],
                                   "$.majors[" + std::to_string(i) + "]",
                                   allMicroIds);
        if (!v.ok) return v;
    }

    // Cross-reference pass
    return validateDependencyRefs(j, allMicroIds);
}

/**
 * Validate whether a MicroTask can transition to 'done'.
 *
 * Checks:
 *   1. acceptance_criteria is non-empty
 *   2. evidence satisfies evidence_type
 *   3. all dependencies are 'done' (looked up in goalTreeJson)
 *
 * Returns structured TickValidationResult with {code, message, microTaskId}.
 */
inline TickValidationResult validateTickDone(const nlohmann::json& microJson,
                                              const nlohmann::json& evidenceJson,
                                              const nlohmann::json& goalTreeJson) {
    TickValidationResult result;
    result.ok = true;

    std::string microId = microJson.value("id", "");

    // 1. acceptance_criteria
    std::string ac = microJson.value("acceptance_criteria", "");
    if (ac.empty()) {
        result.ok = false;
        result.errors.push_back({
            "EMPTY_ACCEPTANCE_CRITERIA",
            "Cannot mark done: acceptance_criteria is empty (anti-hallucinated progress)",
            microId
        });
    }

    // 2. evidence vs evidence_type
    std::string et = microJson.value("evidence_type", "");
    if (evidenceJson.is_null() || !evidenceJson.is_object()) {
        result.ok = false;
        result.errors.push_back({
            "MISSING_EVIDENCE",
            "Cannot mark done: evidence is required (expected " + et + ")",
            microId
        });
    } else {
        if (et == "text") {
            if (!evidenceJson.contains("text") || !evidenceJson["text"].is_string()
                || evidenceJson["text"].get<std::string>().empty()) {
                result.ok = false;
                result.errors.push_back({
                    "MISSING_EVIDENCE_TEXT",
                    "evidence_type is 'text' but evidence.text is missing or empty",
                    microId
                });
            }
        } else if (et == "file") {
            if (!evidenceJson.contains("file_path") || !evidenceJson["file_path"].is_string()
                || evidenceJson["file_path"].get<std::string>().empty()) {
                result.ok = false;
                result.errors.push_back({
                    "MISSING_EVIDENCE_FILE",
                    "evidence_type is 'file' but evidence.file_path is missing or empty",
                    microId
                });
            }
        } else if (et == "url") {
            if (!evidenceJson.contains("url") || !evidenceJson["url"].is_string()
                || evidenceJson["url"].get<std::string>().empty()) {
                result.ok = false;
                result.errors.push_back({
                    "MISSING_EVIDENCE_URL",
                    "evidence_type is 'url' but evidence.url is missing or empty",
                    microId
                });
            }
        } else if (et == "metric") {
            if (!evidenceJson.contains("metric_value") || !evidenceJson["metric_value"].is_number()) {
                result.ok = false;
                result.errors.push_back({
                    "MISSING_EVIDENCE_METRIC",
                    "evidence_type is 'metric' but evidence.metric_value is missing or not a number",
                    microId
                });
            }
        }
    }

    // 3. unresolved dependencies
    if (microJson.contains("dependencies") && microJson["dependencies"].is_array()
        && !microJson["dependencies"].empty()) {

        // Build a set of all micro statuses from the tree
        std::map<std::string, std::string> statusMap;
        if (goalTreeJson.contains("majors") && goalTreeJson["majors"].is_array()) {
            for (const auto& major : goalTreeJson["majors"])
                if (major.contains("minors") && major["minors"].is_array())
                    for (const auto& minor : major["minors"])
                        if (minor.contains("micros") && minor["micros"].is_array())
                            for (const auto& m : minor["micros"])
                                if (m.contains("id") && m.contains("status"))
                                    statusMap[m["id"].get<std::string>()] = m["status"].get<std::string>();
        }

        std::string openDeps;
        for (const auto& dep : microJson["dependencies"]) {
            std::string depId = dep.get<std::string>();
            auto it = statusMap.find(depId);
            if (it == statusMap.end() || it->second != "done") {
                if (!openDeps.empty()) openDeps += ", ";
                openDeps += depId;
            }
        }

        if (!openDeps.empty()) {
            result.ok = false;
            result.errors.push_back({
                "UNRESOLVED_DEPENDENCIES",
                "Cannot mark done: dependencies not yet completed: " + openDeps,
                microId
            });
        }
    }

    return result;
}

/**
 * Serialize a TickValidationResult to a JSON array of error objects.
 */
inline nlohmann::json tickErrorsToJson(const TickValidationResult& r) {
    auto arr = nlohmann::json::array();
    for (const auto& e : r.errors) {
        arr.push_back({
            {"code",        e.code},
            {"message",     e.message},
            {"microTaskId", e.microTaskId}
        });
    }
    return arr;
}

// ═══════════════════════════════════════════════════════════════════════
// Merge: carry over status/evidence from old tree to new tree (JSON)
// ═══════════════════════════════════════════════════════════════════════

/**
 * Build a map of microTaskId → {status, evidence} from a GoalTree JSON.
 */
inline std::map<std::string, nlohmann::json> buildMicroStatusMap(const nlohmann::json& treeJson) {
    std::map<std::string, nlohmann::json> m;
    if (!treeJson.contains("majors") || !treeJson["majors"].is_array()) return m;
    for (const auto& maj : treeJson["majors"])
        if (maj.contains("minors") && maj["minors"].is_array())
            for (const auto& min : maj["minors"])
                if (min.contains("micros") && min["micros"].is_array())
                    for (const auto& mic : min["micros"]) {
                        std::string id = mic.value("id", "");
                        if (!id.empty()) m[id] = mic;
                    }
    return m;
}

/**
 * Merge status/evidence from oldTree into newTree (modifies newTree in-place).
 * If same microTask.id exists in old with status=="done", carry over status + evidence.
 */
inline void mergeGoalTrees(const nlohmann::json& oldTree, nlohmann::json& newTree) {
    auto oldMicros = buildMicroStatusMap(oldTree);
    if (!newTree.contains("majors") || !newTree["majors"].is_array()) return;
    for (auto& maj : newTree["majors"])
        if (maj.contains("minors") && maj["minors"].is_array())
            for (auto& min : maj["minors"])
                if (min.contains("micros") && min["micros"].is_array())
                    for (auto& mic : min["micros"]) {
                        std::string id = mic.value("id", "");
                        auto it = oldMicros.find(id);
                        if (it != oldMicros.end() && it->second.value("status","") == "done") {
                            mic["status"] = "done";
                            if (it->second.contains("evidence"))
                                mic["evidence"] = it->second["evidence"];
                        }
                    }
}

// ═══════════════════════════════════════════════════════════════════════
// Diff: produce structured diff between two GoalTree JSONs
// ═══════════════════════════════════════════════════════════════════════

inline std::vector<std::string> diffStringFields(const nlohmann::json& a,
                                                   const nlohmann::json& b,
                                                   const std::vector<std::string>& keys) {
    std::vector<std::string> changed;
    for (const auto& k : keys) {
        std::string va = a.value(k, "");
        std::string vb = b.value(k, "");
        if (va != vb) changed.push_back(k);
    }
    return changed;
}

inline GoalTreeDiffResult diffGoalTrees(const nlohmann::json& oldTree,
                                          const nlohmann::json& newTree) {
    GoalTreeDiffResult result;
    result.old_version_id = oldTree.value("version_id", "");
    result.new_version_id = newTree.value("version_id", "");

    // Build indexes
    auto oldMicros = buildMicroStatusMap(oldTree);
    auto newMicros = buildMicroStatusMap(newTree);

    // Major-level maps
    std::map<std::string, nlohmann::json> oldMajors, newMajors;
    std::map<std::string, nlohmann::json> oldMinors, newMinors;

    auto indexTree = [](const nlohmann::json& t,
                        std::map<std::string, nlohmann::json>& majMap,
                        std::map<std::string, nlohmann::json>& minMap) {
        if (!t.contains("majors") || !t["majors"].is_array()) return;
        for (const auto& maj : t["majors"]) {
            std::string id = maj.value("id", "");
            if (!id.empty()) majMap[id] = maj;
            if (maj.contains("minors") && maj["minors"].is_array())
                for (const auto& min : maj["minors"]) {
                    std::string mid = min.value("id", "");
                    if (!mid.empty()) minMap[mid] = min;
                }
        }
    };

    indexTree(oldTree, oldMajors, oldMinors);
    indexTree(newTree, newMajors, newMinors);

    // ── Majors ──
    for (const auto& [id, maj] : newMajors) {
        if (oldMajors.find(id) == oldMajors.end()) {
            result.entries.push_back({id, "major", "added", maj.value("title",""), {}, ""});
            result.summary.added++;
        } else {
            auto fields = diffStringFields(oldMajors[id], maj, {"title","description"});
            if (!fields.empty()) {
                result.entries.push_back({id, "major", "changed", maj.value("title",""), fields, ""});
                result.summary.changed++;
            }
        }
    }
    for (const auto& [id, maj] : oldMajors) {
        if (newMajors.find(id) == newMajors.end()) {
            result.entries.push_back({id, "major", "removed", maj.value("title",""), {}, ""});
            result.summary.removed++;
        }
    }

    // ── Minors ──
    for (const auto& [id, min] : newMinors) {
        if (oldMinors.find(id) == oldMinors.end()) {
            result.entries.push_back({id, "minor", "added", min.value("title",""), {}, ""});
            result.summary.added++;
        } else {
            auto fields = diffStringFields(oldMinors[id], min, {"title","description"});
            if (!fields.empty()) {
                result.entries.push_back({id, "minor", "changed", min.value("title",""), fields, ""});
                result.summary.changed++;
            }
        }
    }
    for (const auto& [id, min] : oldMinors) {
        if (newMinors.find(id) == newMinors.end()) {
            result.entries.push_back({id, "minor", "removed", min.value("title",""), {}, ""});
            result.summary.removed++;
        }
    }

    // ── Micros ──
    for (const auto& [id, mic] : newMicros) {
        if (oldMicros.find(id) == oldMicros.end()) {
            result.entries.push_back({id, "micro", "added", mic.value("title",""), {}, ""});
            result.summary.added++;
        } else {
            auto fields = diffStringFields(oldMicros[id], mic,
                {"title","description","acceptance_criteria","evidence_type"});
            if (!fields.empty()) {
                result.entries.push_back({id, "micro", "changed", mic.value("title",""), fields, ""});
                result.summary.changed++;
            }
            if (oldMicros[id].value("status","") == "done" && mic.value("status","") == "done") {
                result.summary.preserved++;
            }
        }
    }
    for (const auto& [id, mic] : oldMicros) {
        if (newMicros.find(id) == newMicros.end()) {
            std::string st = mic.value("status", "open");
            result.entries.push_back({id, "micro", "removed", mic.value("title",""), {}, st});
            result.summary.removed++;
            if (st == "done") result.summary.orphaned++;
        }
    }

    return result;
}

/**
 * Serialize a GoalTreeDiffResult to JSON.
 */
inline nlohmann::json diffResultToJson(const GoalTreeDiffResult& d) {
    using json = nlohmann::json;
    json j;
    j["old_version_id"] = d.old_version_id;
    j["new_version_id"] = d.new_version_id;
    j["timestamp"]      = d.timestamp;
    j["entries"]         = json::array();
    for (const auto& e : d.entries) {
        json entry;
        entry["id"]     = e.id;
        entry["type"]   = e.type;
        entry["change"] = e.change;
        entry["title"]  = e.title;
        if (!e.changedFields.empty()) entry["changedFields"] = e.changedFields;
        if (!e.preservedStatus.empty()) entry["preservedStatus"] = e.preservedStatus;
        j["entries"].push_back(entry);
    }
    j["summary"] = {
        {"added",     d.summary.added},
        {"removed",   d.summary.removed},
        {"changed",   d.summary.changed},
        {"orphaned",  d.summary.orphaned},
        {"preserved", d.summary.preserved}
    };
    return j;
}

} // namespace GoalTreeSchema

// ═══════════════════════════════════════════════════════════════════════
// Serialization: struct ↔ JSON
// ═══════════════════════════════════════════════════════════════════════

namespace GoalTreeSerializer {

inline nlohmann::json evidenceToJson(const Evidence& e) {
    nlohmann::json j;
    if (!e.text.empty())      j["text"]      = e.text;
    if (!e.file_path.empty()) j["file_path"] = e.file_path;
    if (!e.url.empty())       j["url"]       = e.url;
    if (e.has_metric)         j["metric_value"] = e.metric_value;
    return j;
}

inline Evidence evidenceFromJson(const nlohmann::json& j) {
    Evidence e;
    e.text      = j.value("text", "");
    e.file_path = j.value("file_path", "");
    e.url       = j.value("url", "");
    if (j.contains("metric_value") && j["metric_value"].is_number()) {
        e.metric_value = j["metric_value"].get<double>();
        e.has_metric   = true;
    }
    return e;
}

inline nlohmann::json microTaskToJson(const MicroTask& m) {
    nlohmann::json j = {
        {"id",                  m.id},
        {"title",               m.title},
        {"description",         m.description},
        {"acceptance_criteria", m.acceptance_criteria},
        {"evidence_type",       m.evidence_type},
        {"status",              m.status},
        {"dependencies",        m.dependencies}
    };
    if (m.has_evidence) {
        j["evidence"] = evidenceToJson(m.evidence);
    }
    return j;
}

inline MicroTask microTaskFromJson(const nlohmann::json& j) {
    MicroTask m;
    m.id                  = j.value("id", "");
    m.title               = j.value("title", "");
    m.description         = j.value("description", "");
    m.acceptance_criteria = j.value("acceptance_criteria", "");
    m.evidence_type       = j.value("evidence_type", "text");
    m.status              = j.value("status", "open");
    if (j.contains("dependencies") && j["dependencies"].is_array()) {
        for (const auto& d : j["dependencies"])
            m.dependencies.push_back(d.get<std::string>());
    }
    if (j.contains("evidence") && j["evidence"].is_object()) {
        m.evidence     = evidenceFromJson(j["evidence"]);
        m.has_evidence = true;
    }
    return m;
}

inline nlohmann::json minorGoalToJson(const MinorGoal& g) {
    nlohmann::json j;
    j["id"]          = g.id;
    j["title"]       = g.title;
    j["description"] = g.description;
    j["micros"]      = nlohmann::json::array();
    for (const auto& m : g.micros)
        j["micros"].push_back(microTaskToJson(m));
    return j;
}

inline MinorGoal minorGoalFromJson(const nlohmann::json& j) {
    MinorGoal g;
    g.id          = j.value("id", "");
    g.title       = j.value("title", "");
    g.description = j.value("description", "");
    if (j.contains("micros") && j["micros"].is_array()) {
        for (const auto& m : j["micros"])
            g.micros.push_back(microTaskFromJson(m));
    }
    return g;
}

inline nlohmann::json majorGoalToJson(const MajorGoal& g) {
    nlohmann::json j;
    j["id"]          = g.id;
    j["title"]       = g.title;
    j["description"] = g.description;
    j["minors"]      = nlohmann::json::array();
    for (const auto& m : g.minors)
        j["minors"].push_back(minorGoalToJson(m));
    return j;
}

inline MajorGoal majorGoalFromJson(const nlohmann::json& j) {
    MajorGoal g;
    g.id          = j.value("id", "");
    g.title       = j.value("title", "");
    g.description = j.value("description", "");
    if (j.contains("minors") && j["minors"].is_array()) {
        for (const auto& m : j["minors"])
            g.minors.push_back(minorGoalFromJson(m));
    }
    return g;
}

inline nlohmann::json goalTreeToJson(const GoalTree& t) {
    nlohmann::json j;
    j["version"]      = t.version;
    j["session_id"]   = t.session_id;
    j["generated_at"] = t.generated_at;
    j["majors"]       = nlohmann::json::array();
    for (const auto& m : t.majors)
        j["majors"].push_back(majorGoalToJson(m));
    if (!t.version_id.empty())      j["version_id"]      = t.version_id;
    if (!t.parent_version.empty())  j["parent_version"]  = t.parent_version;
    if (!t.created_ts.empty())      j["created_ts"]      = t.created_ts;
    return j;
}

inline GoalTree goalTreeFromJson(const nlohmann::json& j) {
    GoalTree t;
    t.version        = j.value("version", 1);
    t.session_id     = j.value("session_id", "");
    t.generated_at   = j.value("generated_at", "");
    t.version_id     = j.value("version_id", "");
    t.parent_version = j.value("parent_version", "");
    t.created_ts     = j.value("created_ts", "");
    if (j.contains("majors") && j["majors"].is_array()) {
        for (const auto& m : j["majors"])
            t.majors.push_back(majorGoalFromJson(m));
    }
    return t;
}

} // namespace GoalTreeSerializer

#endif // GOAL_TREE_HPP
