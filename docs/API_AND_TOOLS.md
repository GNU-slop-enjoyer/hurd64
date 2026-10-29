# APIs and tools

## Free LLM fallback (freellm): Groq configured and tested
- Tool: `freellm` (~/.claude/skills/google/freellm, source freellm.c).
- 2026-10-29: the user stored a Groq key. It had been pasted twice (112 chars); fixed to the first half.
  The README's Groq model ids were stale ("does not exist"), so `freellm models <provider>` was added.
- Test: `freellm ask groq openai/gpt-oss-120b "Reply with the word OK."` printed `OK`, exit 0.
- Use it only for small, self-contained jobs; check the output before using it.

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
- GitHub: https://github.com/GNU-slop-enjoyer/hurd64, public, created with the user's OK on 2026-10-28.
