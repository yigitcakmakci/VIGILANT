#include "AI/AutoTickerVerifier.hpp"
#include "AI/GeminiService.hpp"
#include <windows.h>
#include <sstream>

using json = nlohmann::json;

static void DebugLogVerifier(const std::string& msg) {
    OutputDebugStringA(("[AutoTickerVerifier] " + msg + "\n").c_str());
}

// ── Constructor ────────────────────────────────────────────────────────

AutoTickerVerifier::AutoTickerVerifier(GeminiService* gemini)
    : m_gemini(gemini) {}

// ═══════════════════════════════════════════════════════════════════════
// Prompt — the most critical part.
//
// Key design decisions:
//   1. STRICT: only PASS when the text *explicitly* satisfies the
//      acceptance criteria.  Implicit / partial / tangential ≠ PASS.
//   2. EVIDENCE: on PASS, the LLM must quote exact spans from the
//      journal text (start offset, length, text).
//   3. CONFIDENCE: 0.0–1.0.  Low confidence even on PASS means the
//      UI must NOT auto-tick.
//   4. NO MUTATION: the prompt never instructs the LLM to mark done.
// ═══════════════════════════════════════════════════════════════════════

std::string AutoTickerVerifier::buildVerifyPrompt(
    const std::string& journalText,
    const std::string& microTaskTitle,
    const std::string& acceptanceCriteria,
    const std::string& evidenceType)
{
    std::ostringstream p;
    p << "You are AcceptanceCriteriaVerifier, a strict evidence-checking engine.\n\n"

      << "TASK:\n"
      << "Determine whether the JOURNAL TEXT below **explicitly** satisfies\n"
      << "the ACCEPTANCE CRITERIA of a micro-task.\n\n"

      << "MICRO-TASK TITLE: \"" << microTaskTitle << "\"\n"
      << "ACCEPTANCE CRITERIA: \"" << acceptanceCriteria << "\"\n"
      << "EXPECTED EVIDENCE TYPE: \"" << evidenceType << "\"\n\n"

      << "JOURNAL TEXT:\n\"\"\"\n" << journalText << "\n\"\"\"\n\n"

      << "RULES (you MUST follow every one):\n"
      << "1. verdict = \"pass\" ONLY IF the journal text contains concrete,\n"
      << "   explicit evidence that the acceptance criteria is met.\n"
      << "   Vague references, partial progress, or tangentially related\n"
      << "   statements are NOT sufficient — verdict must be \"fail\".\n"
      << "2. verdict = \"fail\" if there is any doubt, ambiguity, or if the\n"
      << "   evidence is implicit rather than explicit.\n"
      << "3. confidence: a float 0.0–1.0 indicating how certain you are.\n"
      << "   - 0.9–1.0 = the text unambiguously satisfies the criteria.\n"
      << "   - 0.7–0.89 = likely satisfies but some ambiguity.\n"
      << "   - below 0.7 = uncertain or only partial match.\n"
      << "4. evidenceSpans: if verdict is \"pass\", you MUST provide at least\n"
      << "   one span quoting the EXACT text from the journal that constitutes\n"
      << "   evidence.  Each span has {start, length, text} where start is the\n"
      << "   0-based character offset in the journal text, length is the number\n"
      << "   of characters, and text is the exact quoted substring.\n"
      << "   If verdict is \"fail\", evidenceSpans MUST be an empty array [].\n"
      << "5. explanation: 1–2 sentences explaining your reasoning.\n"
      << "6. You must NEVER suggest marking the task as done.\n"
      << "   You only verify whether evidence exists.\n\n"

      << "Respond with ONLY a single JSON object (no markdown, no extra text):\n"
      << "{\n"
      << "  \"verdict\": \"pass\" | \"fail\",\n"
      << "  \"confidence\": <0.0-1.0>,\n"
      << "  \"evidenceSpans\": [\n"
      << "    {\"start\": <int>, \"length\": <int>, \"text\": \"<exact quote>\"}\n"
      << "  ],\n"
      << "  \"explanation\": \"<str>\"\n"
      << "}\n";

    return p.str();
}

// ═══════════════════════════════════════════════════════════════════════
// JSON extraction helper
// ═══════════════════════════════════════════════════════════════════════

std::string AutoTickerVerifier::extractJsonObject(const std::string& raw) {
    std::string cleaned = raw;

    // Strip markdown code fences
    {
        auto pos = cleaned.find("```json");
        if (pos != std::string::npos) {
            cleaned = cleaned.substr(pos + 7);
        } else {
            pos = cleaned.find("```");
            if (pos != std::string::npos)
                cleaned = cleaned.substr(pos + 3);
        }
        auto endPos = cleaned.rfind("```");
        if (endPos != std::string::npos)
            cleaned = cleaned.substr(0, endPos);
    }

    // Find outermost { ... }
    auto objStart = cleaned.find('{');
    auto objEnd   = cleaned.rfind('}');
    if (objStart == std::string::npos || objEnd == std::string::npos || objEnd <= objStart)
        return "";

    return cleaned.substr(objStart, objEnd - objStart + 1);
}

// ═══════════════════════════════════════════════════════════════════════
// Parse AI response
// ═══════════════════════════════════════════════════════════════════════

VerificationResult AutoTickerVerifier::parseVerifyResponse(
    const std::string& responseText,
    const std::string& microTaskId)
{
    VerificationResult result;
    result.microTaskId = microTaskId;
    result.verdict     = "fail";
    result.confidence  = 0.0;

    std::string jsonStr = extractJsonObject(responseText);
    if (jsonStr.empty()) {
        DebugLogVerifier("No JSON object found in AI response");
        result.explanation = "Verification failed: could not parse AI response.";
        return result;
    }

    json obj;
    try {
        obj = json::parse(jsonStr);
    } catch (const std::exception& e) {
        DebugLogVerifier(std::string("JSON parse error: ") + e.what());
        result.explanation = "Verification failed: malformed AI response.";
        return result;
    }

    // verdict
    std::string v = obj.value("verdict", "fail");
    if (v != "pass" && v != "fail") v = "fail";
    result.verdict = v;

    // confidence — clamp [0,1]
    double conf = obj.value("confidence", 0.0);
    if (conf < 0.0) conf = 0.0;
    if (conf > 1.0) conf = 1.0;
    result.confidence = conf;

    // explanation
    result.explanation = obj.value("explanation", "");

    // evidenceSpans (only meaningful when pass)
    if (result.verdict == "pass" &&
        obj.contains("evidenceSpans") && obj["evidenceSpans"].is_array()) {
        for (const auto& span : obj["evidenceSpans"]) {
            EvidenceSpan es;
            es.start  = span.value("start", 0);
            es.length = span.value("length", 0);
            es.text   = span.value("text", "");
            if (!es.text.empty())
                result.evidenceSpans.push_back(std::move(es));
        }
    }

    // Safety: if verdict is pass but no evidence spans were extracted,
    // downgrade to fail (anti-hallucination guard)
    if (result.verdict == "pass" && result.evidenceSpans.empty()) {
        DebugLogVerifier("PASS verdict with no evidence spans — downgrading to FAIL");
        result.verdict = "fail";
        result.explanation += " [Downgraded: AI returned PASS but provided no evidence spans.]";
    }

    return result;
}

// ═══════════════════════════════════════════════════════════════════════
// Public: Verify
// ═══════════════════════════════════════════════════════════════════════

VerificationResult AutoTickerVerifier::Verify(
    const std::string& journalText,
    const std::string& microTaskId,
    const std::string& microTaskTitle,
    const std::string& acceptanceCriteria,
    const std::string& evidenceType) const
{
    VerificationResult fallback;
    fallback.microTaskId = microTaskId;
    fallback.verdict     = "fail";
    fallback.confidence  = 0.0;

    if (journalText.empty()) {
        DebugLogVerifier("Empty journal text");
        fallback.explanation = "No journal text provided.";
        return fallback;
    }

    if (acceptanceCriteria.empty()) {
        DebugLogVerifier("Empty acceptance criteria for " + microTaskId);
        fallback.explanation = "Acceptance criteria is empty — cannot verify.";
        return fallback;
    }

    if (!m_gemini || !m_gemini->isAvailable()) {
        DebugLogVerifier("AI service unavailable");
        fallback.explanation = "AI service unavailable — cannot verify.";
        return fallback;
    }

    DebugLogVerifier("Verifying " + microTaskId + " (criteria: "
                     + acceptanceCriteria.substr(0, 60) + "...)");

    std::string userPrompt = buildVerifyPrompt(
        journalText, microTaskTitle, acceptanceCriteria, evidenceType);

    std::string systemPrompt =
        "You are AcceptanceCriteriaVerifier. "
        "Return ONLY a single valid JSON object. "
        "Never suggest marking tasks as done. "
        "Be strict: only PASS when evidence is explicit and unambiguous.";

    std::string responseText = m_gemini->sendPrompt(systemPrompt, userPrompt);

    if (responseText.empty()) {
        DebugLogVerifier("AI returned empty response for " + microTaskId);
        fallback.explanation = "AI returned empty response.";
        return fallback;
    }

    DebugLogVerifier("AI response length: " + std::to_string(responseText.size()));

    auto result = parseVerifyResponse(responseText, microTaskId);

    DebugLogVerifier("Verdict for " + microTaskId + ": " + result.verdict
                     + " (confidence=" + std::to_string(result.confidence)
                     + ", spans=" + std::to_string(result.evidenceSpans.size()) + ")");

    return result;
}
