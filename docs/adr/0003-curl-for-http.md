# Shell out to `curl` for HTTP transport

jb shells out to `curl` via `popen()` for all API communication instead of linking against libcurl, embedding a TLS library, or implementing HTTP from scratch. This means `curl` is the one runtime dependency beyond POSIX.

The alternatives were: link libcurl (not zero deps), embed a single-file TLS library like tlse (~2000 lines, security minefield), or implement HTTP/TLS from scratch (unreasonable). Shelling out to `curl` is the most honest interpretation of "zero deps" on a POSIX system — `curl` is universally available, handles TLS/HTTPS correctly, supports SSE streaming via `-N`, and `popen()` gives jb line-by-line access to the response. The cost is one subprocess per API call. On low-end hardware, the curl binary is already in memory anyway.
