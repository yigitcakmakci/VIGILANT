#ifndef AUTO_TICKER_VERIFIER_HPP
#define AUTO_TICKER_VERIFIER_HPP

#include <string>
#include <vector>
#include "Utils/json.hpp"

// ═══════════════════════════════════════════════════════════════════════
// AutoTickerVerifier — strict acceptance-criteria verification.
//
// Given a journal text and a single MicroTask (its acceptance_criteria
// and evidence_type), the Verifier asks the LLM:
//
//   "Does the journal text EXPLICITLY satisfy the acceptance criteria?
//    Only PASS if the text contains concrete evidence.
//    If PASS, quote the exact text spans that constitute evidence."
//
// INVARIANT:
//   This module NEVER marks any MicroTask as 'done'.
//   - verdict == 'pass' with HIGH confidence  → UI asks user to confirm
//   - verdict == 'pass' with LOW  confidence  → UI shows warning, asks confirm
//   - verdict == 'fail'                       → UI shows explanation, no auto-tick
//
// The confidence threshold for "high" is defined in kAutoConfirmThreshold.
// Below that threshold the UI MUST require explicit user confirmation.
// ═══════════════════════════════════════════════════════════════════════

// ── Confidence threshold ──────────────────────────────────────────────
static constexpr double kAutoConfirmThreshold = 0.85;

// ── EvidenceSpan — quoted region from journal text ────────────────────

struct EvidenceSpan {
    int         start  = 0;      // character offset in journalText
    int         length = 0;      // span length
    std::string text;            // the exact quoted substring
};

// ── VerificationResult — per-MicroTask verdict ────────────────────────

struct VerificationResult {
    std::string                microTaskId;
    std::string                verdict;        // "pass" | "fail"
    double                     confidence = 0.0; // 0.0–1.0
    std::vector<EvidenceSpan>  evidenceSpans;  // non-empty only when pass
    std::string                explanation;     // human-readable reasoning

    // Convenience: is this a high-confidence pass that could be auto-confirmed?
    bool isHighConfidencePass() const {
        return verdict == "pass" && confidence >= kAutoConfirmThreshold;
    }
};

// ── AutoTickerVerifier ────────────────────────────────────────────────

class GeminiService;

class AutoTickerVerifier {
public:
    explicit AutoTickerVerifier(GeminiService* gemini);

    /// Verify whether journalText explicitly satisfies a MicroTask's
    /// acceptance criteria.  Never mutates any MicroTask.
    VerificationResult Verify(
        const std::string& journalText,
        const std::string& microTaskId,
        const std::string& microTaskTitle,
        const std::string& acceptanceCriteria,
        const std::string& evidenceType) const;

private:
    // Build the strict verification prompt
    static std::string buildVerifyPrompt(
        const std::string& journalText,
        const std::string& microTaskTitle,
        const std::string& acceptanceCriteria,
        const std::string& evidenceType);

    // Parse AI JSON response into VerificationResult
    static VerificationResult parseVerifyResponse(
        const std::string& responseText,
        const std::string& microTaskId);

    // Strip markdown code fences and extract JSON object from raw text
    static std::string extractJsonObject(const std::string& raw);

    GeminiService* m_gemini;
};

#endif // AUTO_TICKER_VERIFIER_HPP
