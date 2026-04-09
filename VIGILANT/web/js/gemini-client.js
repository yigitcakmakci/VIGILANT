/* ═══════════════════════════════════════════════════════════════════════
   VIGILANT – Gemini TS Client (Vanilla JS)
   High-level wrapper around the Gemini generateContent API.
   Combines: config, logger, token-bucket rate limiter, fetch-with-retry,
   and the main GeminiClient class into a single zero-dependency module.
   Works in WebView2 + modern browsers (no Node.js required).
   ═══════════════════════════════════════════════════════════════════════ */

// ═══════════════════════════════════════════════════════════════════════
// Logger – structured JSON logger with error classification
// ═══════════════════════════════════════════════════════════════════════

var GeminiLogger = (function () {
    'use strict';

    var LEVELS = { debug: 0, info: 1, warn: 2, error: 3 };

    function GeminiLogger(minLevel) {
        this._minLevel = minLevel || 'info';
    }

    GeminiLogger.prototype.debug = function (message, meta) {
        this._log('debug', message, meta);
    };

    GeminiLogger.prototype.info = function (message, meta) {
        this._log('info', message, meta);
    };

    GeminiLogger.prototype.warn = function (message, meta) {
        this._log('warn', message, meta);
    };

    GeminiLogger.prototype.error = function (message, err, meta) {
        var errorClass = _geminiClassifyError(err);
        var merged = {};
        if (meta) { for (var k in meta) merged[k] = meta[k]; }
        merged.errorClass = errorClass;
        this._log('error', message, merged);
    };

    GeminiLogger.prototype.logRequest = function (method, latencyMs, ok, meta) {
        var level = ok ? 'info' : 'warn';
        var merged = { latencyMs: latencyMs };
        if (meta) { for (var k in meta) merged[k] = meta[k]; }
        this._log(level, method + ' ' + (ok ? 'OK' : 'FAIL'), merged);
    };

    GeminiLogger.prototype._log = function (level, message, meta) {
        if (LEVELS[level] < LEVELS[this._minLevel]) return;

        var entry = {
            timestamp: new Date().toISOString(),
            level: level,
            message: message
        };
        if (meta && meta.latencyMs != null) entry.latencyMs = meta.latencyMs;
        if (meta && meta.errorClass != null) entry.errorClass = meta.errorClass;
        if (meta) entry.meta = meta;

        var line = JSON.stringify(entry);
        if (level === 'error') console.error(line);
        else if (level === 'warn') console.warn(line);
        else console.log(line);
    };

    return GeminiLogger;
})();

function _geminiClassifyError(err) {
    if (err == null) return 'Unknown';
    if (err instanceof TypeError) return 'TypeError';
    if (err instanceof SyntaxError) return 'ParseError';

    var msg = (err instanceof Error) ? err.message : String(err);
    if (msg.indexOf('AbortError') !== -1 || msg.indexOf('timeout') !== -1) return 'Timeout';
    if (msg.indexOf('ECONNREFUSED') !== -1 || msg.indexOf('ENOTFOUND') !== -1) return 'Network';
    if (msg.indexOf('429') !== -1) return 'RateLimited';
    if (msg.indexOf('401') !== -1 || msg.indexOf('403') !== -1) return 'Auth';
    return 'Unknown';
}

// ═══════════════════════════════════════════════════════════════════════
// Token-Bucket Rate Limiter
// ═══════════════════════════════════════════════════════════════════════

var GeminiRateLimiter = (function () {
    'use strict';

    function GeminiRateLimiter(requestsPerMinute) {
        this._capacity = requestsPerMinute;
        this._tokens = requestsPerMinute;
        this._refillIntervalMs = 60000 / requestsPerMinute;
        this._lastRefillTime = Date.now();
        this._waitQueue = [];
        this._timerId = null;
        this._startRefillLoop();
    }

    GeminiRateLimiter.prototype.acquire = function () {
        this._refill();
        if (this._tokens >= 1) {
            this._tokens -= 1;
            return Promise.resolve();
        }
        var self = this;
        return new Promise(function (resolve) {
            self._waitQueue.push(resolve);
        });
    };

    GeminiRateLimiter.prototype.dispose = function () {
        if (this._timerId !== null) {
            clearInterval(this._timerId);
            this._timerId = null;
        }
        var queue = this._waitQueue.splice(0);
        for (var i = 0; i < queue.length; i++) queue[i]();
    };

    GeminiRateLimiter.prototype._refill = function () {
        var now = Date.now();
        var elapsed = now - this._lastRefillTime;
        var newTokens = Math.floor(elapsed / this._refillIntervalMs);
        if (newTokens > 0) {
            this._tokens = Math.min(this._capacity, this._tokens + newTokens);
            this._lastRefillTime += newTokens * this._refillIntervalMs;
        }
    };

    GeminiRateLimiter.prototype._startRefillLoop = function () {
        var self = this;
        this._timerId = setInterval(function () {
            self._refill();
            while (self._tokens >= 1 && self._waitQueue.length > 0) {
                self._tokens -= 1;
                self._waitQueue.shift()();
            }
        }, this._refillIntervalMs);
    };

    return GeminiRateLimiter;
})();

// ═══════════════════════════════════════════════════════════════════════
// Fetch with Retry – timeout + exponential back-off
// ═══════════════════════════════════════════════════════════════════════

function _geminiFetchIsTransient(status) {
    return status === 429 || (status >= 500 && status < 600);
}

function _geminiFetchJitteredDelay(base, attempt) {
    var exp = base * Math.pow(2, attempt);
    return exp + Math.random() * exp * 0.3;
}

function _geminiFetchSleep(ms) {
    return new Promise(function (r) { setTimeout(r, ms); });
}

/**
 * Fetch with timeout + exponential back-off retry.
 * @param {string} url
 * @param {RequestInit} init
 * @param {{timeoutMs:number, maxRetries:number, baseDelayMs?:number, logger?:GeminiLogger}} opts
 * @returns {Promise<Response>}
 */
function geminiFetchWithRetry(url, init, opts) {
    var timeoutMs = opts.timeoutMs;
    var maxRetries = opts.maxRetries;
    var baseDelayMs = opts.baseDelayMs || 500;
    var logger = opts.logger;

    function attempt(n) {
        var controller = new AbortController();
        var timer = setTimeout(function () { controller.abort(); }, timeoutMs);
        var merged = {};
        for (var k in init) merged[k] = init[k];
        merged.signal = controller.signal;

        var t0 = performance.now();
        return fetch(url, merged).then(function (res) {
            clearTimeout(timer);
            var latencyMs = Math.round(performance.now() - t0);
            if (logger) logger.logRequest('fetch', latencyMs, res.ok, { attempt: n, status: res.status, url: url });
            if (res.ok || !_geminiFetchIsTransient(res.status)) return res;
            // transient → fall through to retry
            if (n >= maxRetries) return res;
            var delay = _geminiFetchJitteredDelay(baseDelayMs, n);
            if (logger) logger.debug('Retry #' + (n + 1) + ' in ' + Math.round(delay) + ' ms');
            return _geminiFetchSleep(delay).then(function () { return attempt(n + 1); });
        }).catch(function (err) {
            clearTimeout(timer);
            var latencyMs = Math.round(performance.now() - t0);
            if (logger) logger.logRequest('fetch', latencyMs, false, { attempt: n, errorClass: _geminiClassifyError(err), url: url });
            if (n >= maxRetries) throw err;
            var delay = _geminiFetchJitteredDelay(baseDelayMs, n);
            if (logger) logger.debug('Retry #' + (n + 1) + ' in ' + Math.round(delay) + ' ms');
            return _geminiFetchSleep(delay).then(function () { return attempt(n + 1); });
        });
    }

    return attempt(0);
}

// ═══════════════════════════════════════════════════════════════════════
// Config Resolver (WebView2-friendly — no Node fs/path)
// Resolution: explicit config → window._geminiConfig → defaults
// ═══════════════════════════════════════════════════════════════════════

var GEMINI_DEFAULTS = {
    model: 'gemini-2.0-flash',
    timeoutMs: 30000,
    maxRetries: 3,
    rateLimitRpm: 60,
    baseUrl: 'https://generativelanguage.googleapis.com/v1beta'
};

/**
 * Resolve Gemini config. In WebView2 the API key can be provided via:
 *   1. Explicit config.apiKey
 *   2. window._geminiConfig.apiKey (set by C++ side via ExecuteScript)
 *
 * @param {object} [partial] – Partial config
 * @returns {object} Resolved config with all fields guaranteed
 */
function _geminiResolveConfig(partial) {
    partial = partial || {};
    var globalCfg = (typeof window !== 'undefined' && window._geminiConfig) || {};

    var apiKey = partial.apiKey || globalCfg.apiKey || '';

    if (!apiKey) {
        console.warn('[GeminiClient] API key not found. Provide via config or window._geminiConfig.apiKey.');
    }

    return {
        apiKey: apiKey,
        model: partial.model || globalCfg.model || GEMINI_DEFAULTS.model,
        timeoutMs: partial.timeoutMs || globalCfg.timeoutMs || GEMINI_DEFAULTS.timeoutMs,
        maxRetries: partial.maxRetries != null ? partial.maxRetries : (globalCfg.maxRetries != null ? globalCfg.maxRetries : GEMINI_DEFAULTS.maxRetries),
        rateLimitRpm: partial.rateLimitRpm || globalCfg.rateLimitRpm || GEMINI_DEFAULTS.rateLimitRpm,
        baseUrl: partial.baseUrl || globalCfg.baseUrl || GEMINI_DEFAULTS.baseUrl
    };
}

// ═══════════════════════════════════════════════════════════════════════
// Gemini Client
// ═══════════════════════════════════════════════════════════════════════

var GeminiClient = (function () {
    'use strict';

    /**
     * @param {object} [config] – Partial config (apiKey, model, etc.)
     * @param {string} [logLevel] – 'debug' | 'info' | 'warn' | 'error'
     */
    function GeminiClient(config, logLevel) {
        this._cfg = _geminiResolveConfig(config);
        this._limiter = new GeminiRateLimiter(this._cfg.rateLimitRpm);
        this._log = new GeminiLogger(logLevel || 'info');
        this._log.info('GeminiClient initialized', { model: this._cfg.model });
    }

    /**
     * Send a generateContent request and return the parsed response.
     * @param {Array} contents – Conversation turns [{role, parts:[{text}]}]
     * @param {object} [generationConfig] – temperature, maxOutputTokens, etc.
     * @param {Array} [safetySettings]
     * @returns {Promise<object>} GenerateContentResponse
     */
    GeminiClient.prototype.generateContent = function (contents, generationConfig, safetySettings) {
        var self = this;
        return this._limiter.acquire().then(function () {
            var url = self._buildUrl();
            var body = { contents: contents };
            if (generationConfig) body.generationConfig = generationConfig;
            if (safetySettings) body.safetySettings = safetySettings;

            var t0 = performance.now();
            return geminiFetchWithRetry(
                url,
                {
                    method: 'POST',
                    headers: { 'Content-Type': 'application/json' },
                    body: JSON.stringify(body)
                },
                {
                    timeoutMs: self._cfg.timeoutMs,
                    maxRetries: self._cfg.maxRetries,
                    logger: self._log
                }
            ).then(function (res) {
                if (!res.ok) {
                    return res.text().catch(function () { return ''; }).then(function (text) {
                        var latencyMs = Math.round(performance.now() - t0);
                        self._log.error('generateContent failed [' + res.status + ']', new Error(text), { latencyMs: latencyMs, status: res.status });
                        throw new Error('[GeminiClient] HTTP ' + res.status + ': ' + text);
                    });
                }
                return res.json().then(function (data) {
                    var latencyMs = Math.round(performance.now() - t0);
                    self._log.logRequest('generateContent', latencyMs, true, {
                        tokens: data.usageMetadata ? data.usageMetadata.totalTokenCount : undefined
                    });
                    return data;
                });
            });
        });
    };

    /**
     * Convenience: send a single user prompt and return the first candidate text.
     * @param {string} text
     * @param {object} [generationConfig]
     * @returns {Promise<string>}
     */
    GeminiClient.prototype.prompt = function (text, generationConfig) {
        return this.generateContent(
            [{ role: 'user', parts: [{ text: text }] }],
            generationConfig
        ).then(function (res) {
            return (res.candidates && res.candidates[0] && res.candidates[0].content &&
                    res.candidates[0].content.parts && res.candidates[0].content.parts[0])
                ? res.candidates[0].content.parts[0].text || ''
                : '';
        });
    };

    /** Release rate-limiter timers. */
    GeminiClient.prototype.dispose = function () {
        this._limiter.dispose();
        this._log.info('GeminiClient disposed');
    };

    // ── Internal ──────────────────────────────────────────────────────

    GeminiClient.prototype._buildUrl = function () {
        return this._cfg.baseUrl + '/models/' + this._cfg.model + ':generateContent?key=' + this._cfg.apiKey;
    };

    return GeminiClient;
})();

// ═══════════════════════════════════════════════════════════════════════
// Narrative Helper – daily activity narrative via Gemini
// ═══════════════════════════════════════════════════════════════════════

var GeminiNarrative = (function () {
    'use strict';

    var NARRATIVE_SYSTEM_PROMPT =
        'You are a productivity analyst. Given a compressed timeline of application usage sessions, ' +
        'produce a JSON response with:\n' +
        '  "narrative": a 5–10 sentence daily summary in second person ("You ..."),\n' +
        '  "insights": exactly 3 actionable insight bullets (array of strings).\n' +
        'Respond with ONLY the JSON object. No markdown fences.';

    /**
     * Compress raw session blocks for the Gemini prompt.
     * @param {Array} sessions – [{start, end, appName, category, focusScore, windowTitle?}]
     * @param {object} [opts] – {maxBlocks, tokenBudget, minBlockMinutes}
     * @returns {Array} Compressed blocks
     */
    function compressSessions(sessions, opts) {
        opts = opts || {};
        var maxBlocks = opts.maxBlocks || 40;
        var minMins = opts.minBlockMinutes || 1;

        var blocks = [];
        for (var i = 0; i < sessions.length && blocks.length < maxBlocks; i++) {
            var s = sessions[i];
            var startDate = new Date(s.start || s.nativeStartMs || 0);
            var endDate = s.end ? new Date(s.end) : new Date(startDate.getTime() + (s.durationMs || 0));
            var mins = Math.round((endDate - startDate) / 60000);
            if (mins < minMins) continue;
            blocks.push({
                start: startDate.toTimeString().substring(0, 5),
                end: endDate.toTimeString().substring(0, 5),
                app: s.appName || s.app || 'unknown',
                cat: s.category || 'unknown',
                focus: s.focusScore != null ? s.focusScore : 0.5,
                mins: mins
            });
        }
        return blocks;
    }

    /**
     * Generate a daily narrative from session data.
     * @param {GeminiClient} client
     * @param {Array} sessions – Raw session blocks
     * @param {object} [opts]
     * @returns {Promise<{narrative:string, insights:string[]}>}
     */
    function generateNarrative(client, sessions, opts) {
        var compressed = compressSessions(sessions, opts);
        if (compressed.length === 0) {
            return Promise.resolve({
                narrative: 'No significant sessions recorded today.',
                insights: ['Start tracking your activity.', 'Open your usual work applications.', 'Check back later for insights.']
            });
        }

        var userMessage = 'Timeline (' + compressed.length + ' blocks):\n' + JSON.stringify(compressed);

        return client.generateContent(
            [
                { role: 'user', parts: [{ text: NARRATIVE_SYSTEM_PROMPT }] },
                { role: 'model', parts: [{ text: 'Understood. Send the timeline data.' }] },
                { role: 'user', parts: [{ text: userMessage }] }
            ],
            { temperature: 0.3, maxOutputTokens: 512 }
        ).then(function (res) {
            var text = (res.candidates && res.candidates[0] && res.candidates[0].content &&
                        res.candidates[0].content.parts && res.candidates[0].content.parts[0])
                ? res.candidates[0].content.parts[0].text || '{}'
                : '{}';
            try {
                var parsed = JSON.parse(text);
                return {
                    narrative: parsed.narrative || '',
                    insights: Array.isArray(parsed.insights) ? parsed.insights.slice(0, 3) : []
                };
            } catch (e) {
                return { narrative: text, insights: [] };
            }
        });
    }

    return {
        compressSessions: compressSessions,
        generateNarrative: generateNarrative
    };
})();
