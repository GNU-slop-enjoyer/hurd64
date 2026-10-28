# APIs and tools

## Free LLM fallback (freellm): NOT configured
- Tool: `freellm` (~/.claude/skills/google/freellm, source freellm.c). Lists the
  awesome-freellm-apis providers and sends one OpenAI-style chat request with a stored key.
- Checked 2026-10-28: `freellm keys` -> "no keys". `freellm ask groq ...` and `freellm ask gemini ...`
  both fail with "no key for ..." (exit 1). Only the fake-key error path was ever tested.
- To enable: the user runs `freellm key groq` (or another provider) in their own terminal.
  Then test with `freellm ask groq <model> "Reply with the word OK."` before relying on it.

## Why there is no automatic takeover at the usage limit
When Claude Code reaches its limit the model stops answering. A hook can run a script, but no
script can continue the session's reasoning, tools and permissions with another model, and a
free chat API is not an agent that can safely drive this machine. Supported instead:
1. A checkpoint (PROJECT_STATUS.md, NEXT_SESSION.md) updated after every verified step, so
   "continue" after the limit resets picks up exactly where work stopped.
2. `freellm ask` for small, self-contained jobs (summaries, boilerplate) once a key exists;
   results get checked before use.

## Other tools
- Context7 connector: library docs (added 2026-10-28, not used yet).
- Gmail / Calendar / Drive: connected; no project material found (empty Drive, empty calendar).
- GitHub: https://github.com/GNU-slop-enjoyer/tuxhurd, public, created with the user's OK on 2026-10-28.
