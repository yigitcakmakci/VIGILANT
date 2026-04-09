#include "AI/AutoTickerMatcher.hpp"
#include "AI/GeminiService.hpp"
#include <windows.h>
#include <algorithm>
#include <sstream>

using json = nlohmann::json;

static void DebugLogMatcher(const std::string& msg) {
    OutputDebugStringA(("[AutoTickerMatcher] " + msg + "\n").c_str());
}

// ── Constructor ────────────────────────────────────────────────────────

AutoTickerMatcher::AutoTickerMatcher(GeminiService* gemini)
    : m_gemini(gemini) {}

// ── Collect open MicroTasks from the GoalTree ──────────────────────────

std::vector<AutoTickerMatcher::OpenMicro>
AutoTickerMatcher::collectOpenMicros(const GoalTree& tree) {
    std::vector<OpenMicro> result;
    for (const auto& major : tree.majors) {
        for (const auto& minor : major.minors) {
            for (const auto& micro : minor.micros) {
                if (micro.status == "open") {
                    result.push_back({
                        micro.id,
                        micro.title,
                        micro.description,
                        micro.acceptance_criteria
                    });
                }
            }
        }
    }
    return result;
}

// ── Build the AI prompt ────────────────────────────────────────────────

std::string AutoTickerMatcher::buildMatchPrompt(
    const std::string& journalText,
    const std::vector<OpenMicro>& micros) const
{
    // Build a JSON array of open micros for the prompt
    json microsArray = json::array();
    for (const auto& m : micros) {
        microsArray.push_back({
            {"id",                  m.id},
            {"title",               m.title},
            {"description",         m.description},
            {"acceptance_criteria", m.acceptance_criteria}
        });
    }

    std::ostringstream prompt;
    prompt
        << "You are a semantic matching engine. Given a user's journal entry and a "
        << "list of open micro-tasks, determine which micro-tasks the journal text "
        << "provides evidence for.\n\n"
        << "RULES:\n"
        << "- Only return tasks where there is a genuine semantic connection.\n"
        << "- Score each match from 0.0 to 1.0 (confidence).\n"
        << "- For each match, identify the exact text span(s) in the journal that "
        << "support the match (start offset, length, and the text itself).\n"
        << "- Provide a short rationale (1-2 sentences) for each match.\n"
        << "- If no tasks match, return an empty array.\n"
        << "- DO NOT mark any task as done. Only suggest candidates.\n\n"
        << "JOURNAL TEXT:\n\"\"\"" << journalText << "\"\"\"\n\n"
        << "OPEN MICRO-TASKS:\n" << microsArray.dump(2) << "\n\n"
        << "Respond with ONLY a JSON array (no markdown, no explanation outside JSON):\n"
        << "[\n"
        << "  {\n"
        << "    \"microTaskId\": \"<id>\",\n"
        << "    \"score\": <0.0-1.0>,\n"
        << "    \"matchedSpans\": [{\"start\": <int>, \"length\": <int>, \"text\": \"<str>\"}],\n"
        << "    \"rationale\": \"<str>\"\n"
        << "  }\n"
        << "]\n";

    return prompt.str();
}

// ── Parse AI response ──────────────────────────────────────────────────

std::vector<MatchCandidate>
AutoTickerMatcher::parseMatchResponse(const std::string& responseText) {
    std::vector<MatchCandidate> candidates;

    // Strip markdown code fences if present
    std::string cleaned = responseText;
    {
        auto pos = cleaned.find("```json");
        if (pos != std::string::npos) {
            cleaned = cleaned.substr(pos + 7);
        } else {
            pos = cleaned.find("```");
            if (pos != std::string::npos) {
                cleaned = cleaned.substr(pos + 3);
            }
        }
        auto endPos = cleaned.rfind("```");
        if (endPos != std::string::npos) {
            cleaned = cleaned.substr(0, endPos);
        }
    }

    // Find the JSON array boundaries
    auto arrStart = cleaned.find('[');
    auto arrEnd   = cleaned.rfind(']');
    if (arrStart == std::string::npos || arrEnd == std::string::npos || arrEnd <= arrStart) {
        DebugLogMatcher("No JSON array found in response");
        return candidates;
    }

    std::string jsonStr = cleaned.substr(arrStart, arrEnd - arrStart + 1);
    json arr;
    try {
        arr = json::parse(jsonStr);
    } catch (const std::exception& e) {
        DebugLogMatcher(std::string("JSON parse error: ") + e.what());
        return candidates;
    }

    if (!arr.is_array()) {
        DebugLogMatcher("Response is not a JSON array");
        return candidates;
    }

    for (const auto& item : arr) {
        MatchCandidate c;
        c.microTaskId = item.value("microTaskId", "");
        c.score       = item.value("score", 0.0);
        c.rationale   = item.value("rationale", "");

        if (c.microTaskId.empty()) continue;

        // Clamp score
        if (c.score < 0.0) c.score = 0.0;
        if (c.score > 1.0) c.score = 1.0;

        // Parse matched spans
        if (item.contains("matchedSpans") && item["matchedSpans"].is_array()) {
            for (const auto& span : item["matchedSpans"]) {
                MatchedSpan ms;
                ms.start  = span.value("start", 0);
                ms.length = span.value("length", 0);
                ms.text   = span.value("text", "");
                c.matchedSpans.push_back(std::move(ms));
            }
        }

        candidates.push_back(std::move(c));
    }

    // Sort descending by score
    std::sort(candidates.begin(), candidates.end(),
        [](const MatchCandidate& a, const MatchCandidate& b) {
            return a.score > b.score;
        });

    return candidates;
}

// ── Public: Match ──────────────────────────────────────────────────────

std::vector<MatchCandidate> AutoTickerMatcher::Match(
    const std::string& journalText,
    const GoalTree&    goalTree) const
{
    if (journalText.empty()) {
        DebugLogMatcher("Empty journal text, skipping");
        return {};
    }

    auto openMicros = collectOpenMicros(goalTree);
    if (openMicros.empty()) {
        DebugLogMatcher("No open micro-tasks in GoalTree, skipping");
        return {};
    }

    DebugLogMatcher("Matching journal against " + std::to_string(openMicros.size()) + " open micros");

    if (!m_gemini || !m_gemini->isAvailable()) {
        DebugLogMatcher("AI service unavailable, returning empty candidates");
        return {};
    }

    // Build prompt and call AI
    std::string prompt = buildMatchPrompt(journalText, openMicros);

    std::string systemPrompt =
        "You are a semantic matching engine. Return ONLY valid JSON. "
        "Never mark tasks as done. Only produce candidate suggestions.";

    std::string responseText = m_gemini->sendPrompt(systemPrompt, prompt);

    if (responseText.empty()) {
        DebugLogMatcher("AI returned empty response");
        return {};
    }

    DebugLogMatcher("AI response length: " + std::to_string(responseText.size()));

    auto candidates = parseMatchResponse(responseText);
    DebugLogMatcher("Produced " + std::to_string(candidates.size()) + " match candidates");

    return candidates;
}
