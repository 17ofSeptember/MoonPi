# Needle 2 inspection and integration status

Inspected upstream HEAD `53df049c4a1a82fca1027b81f9ff21336dfb0861` on 2026-09-08.
Sources: [README](https://github.com/cactus-compute/needle),
[API](https://github.com/cactus-compute/needle/blob/53df049c4a1a82fca1027b81f9ff21336dfb0861/doc/apis.md),
[native worker bindings](https://github.com/cactus-compute/needle/blob/53df049c4a1a82fca1027b81f9ff21336dfb0861/needle/_worker.py).

The ctypes generation-2 branch declares this native contract:

```cpp
int needle_load(const char* archive_bytes, uint64_t byte_count);
int needle_init(const char* system, const char* tools_json, const char* index_path);
int needle_complete(const char* text, int token_limit, char* output, int capacity);
void needle_reset();
```

These are translations of observed bindings, not a shipped adapter. Load takes
archive bytes, not a filename. Negative return codes are errors. Generation 3 has
a different completion signature. Compatibility must be pinned and verified before
calling native symbols. No engine binary or weights have been downloaded or run.

Moon Pi currently supplies IAiEngine, UnavailableAiEngine and test-only MockAiEngine.
Manual operation is independent of Needle. The native worker, response validation,
confidence policy, circuit breaker and reviewed graph proposals remain unimplemented.
Do not install the Python package as a Moon Pi runtime dependency. The planned C++
worker will load locally supplied, verified generation-2 assets with process
isolation and bounded cancellation. It will return constrained proposals only;
the application compiler and user approval remain authoritative.
