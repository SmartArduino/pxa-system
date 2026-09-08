# ESP-IDF Integration

The repository root is the backend-neutral ESP component. Optional adapters
that require extra ESP components live below `components/` so a product opts
into those dependencies explicitly through `EXTRA_COMPONENT_DIRS`.

Board services, the WAMR worker, package storage and a concrete system UI should
be separate product components. They may replace reference providers by normal
registration and policy, without overriding or weak-linking core symbols.
