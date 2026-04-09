#ifndef AUTO_TICKER_MATCHER_HPP
#define AUTO_TICKER_MATCHER_HPP

#include <string>
#include <vector>
#include "AI/GoalTree.hpp"
#include "Utils/json.hpp"

// ═══════════════════════════════════════════════════════════════════════
// AutoTickerMatcher — semantic matching of journal text to open MicroTasks.
//
// INVARIANT:
//   This module NEVER marks any MicroTask as 'done'.
//   It only produces ranked candidate suggestions for the user to review.
//
// Input:  journalText  — free-form user journal entry
//         goalTree     — current GoalTree (only 'open' micros considered)
// Output: ranked list of MatchCandidate, sorted by descending score.
// ═══════════════════════════════════════════════════════════════════════

// ── MatchedSpan — region in journalText that triggered a match ────────

struct MatchedSpan {
    int         start  = 0;     // character offset in journalText
    int         length = 0;     // span length in characters
    std::string text;           // the matched substring (convenience)
};

// ── MatchCandidate — a single micro-task match suggestion ─────────────

struct MatchCandidate {
    std::string              microTaskId;
    double                   score = 0.0;   // 0.0–1.0 confidence
    std::vector<MatchedSpan> matchedSpans;
    std::string              rationale;      // human-readable explanation
};

// ── AutoTickerMatcher ─────────────────────────────────────────────────

class GeminiService;

class AutoTickerMatcher {
public:
    explicit AutoTickerMatcher(GeminiService* gemini);

    /// Produce ranked candidates.  Never mutates goalTree.
    /// Returns empty vector when no open micros exist or AI is unavailable.
    std::vector<MatchCandidate> Match(
        const std::string& journalText,
        const GoalTree&    goalTree) const;

private:
    // Collect all open MicroTasks from the tree
    struct OpenMicro {
        std::string id;
        std::string title;
        std::string description;
        std::string acceptance_criteria;
    };
    static std::vector<OpenMicro> collectOpenMicros(const GoalTree& tree);

    // Build the AI prompt for semantic matching
    std::string buildMatchPrompt(
        const std::string& journalText,
        const std::vector<OpenMicro>& micros) const;

    // Parse AI JSON response into MatchCandidate vector
    static std::vector<MatchCandidate> parseMatchResponse(
        const std::string& responseText);

    GeminiService* m_gemini;
};

#endif // AUTO_TICKER_MATCHER_HPP
