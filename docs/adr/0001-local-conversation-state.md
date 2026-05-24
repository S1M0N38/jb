# Local conversation state with Chat Completions

jb manages the full conversation history locally in an append-only JSONL file (`state.jsonl`). The entire message array is sent with every API call. The API is stateless — no server-side session management, no `previous_response_id`.

This means jb works with **any** OpenAI Chat Completions-compatible endpoint: OpenAI, DeepSeek, Groq, Together, Ollama, llama.cpp, and any future provider. The trade-off is that the state file grows with every turn — a long session with file reads could reach hundreds of kilobytes. On low-end hardware this is acceptable because the file is append-only (one `fprintf` per message) and read sequentially on startup. The alternative was Open Responses with `previous_response_id` (server-side state), which locked jb into providers supporting that spec and excluded local models entirely.
