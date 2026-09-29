# PXA Storage Draft 0.1

Storage v1 is service 6: a small persistent key/value store scoped to the
signed App identity. Every Component in one App sees the same store; another
App, even with the same textual App ID but a different publisher, does not.
The desktop product simulator keys its persistent Storage directory by both
the verified publisher key ID and App ID.
Storage is private data, so the App Manager's clear-data action removes it
along with private files. It is not a directory, cache, database query engine
or Secrets facility.

## Keys, values and limits

A key is 1 to 64 ASCII bytes. Its first byte is an ASCII letter; later bytes
may be ASCII letters, digits, `.`, `_` or `-`. Comparison and list order are
raw unsigned-byte lexicographic order. A value is an opaque byte string and
may be empty. Hosts must bound one value and the total value bytes for an App;
the wire format permits up to 2048 value bytes. The desktop simulator uses the
full 2048-byte limit, while ESP firmware currently configures 960 bytes. Hosts
reject values above their configured per-value bound and report
`quota-exceeded` when an update exceeds the configured App total.

The Host persists a whole replacement snapshot before reporting successful
`set` or `remove`. It keeps recovery data outside the Guest FS namespace and
outside FS v1's logical-file quota. A malformed or unavailable snapshot makes
Storage requests fail; it never becomes a partially decoded Guest result.

## Requests

All operations have a nonzero request ID and complete with the Core common
leading `status:i32`. `get`, `set` and `remove` require one `key` record;
`set` also requires one `value` record. `list` accepts zero or one `key`
cursor record. A cursor means strictly after that raw-byte key.
Unknown optional records may be ignored; unknown required records are
unsupported. Record order is ascending raw tag order.

- `get` returns one `value` record on success and `not-found` when absent.
- `set` creates or replaces a value and returns only success status.
- `remove` returns only success status, or `not-found` when absent.
- `list` returns at most 14 `key` records in strict lexicographic order. The
  Guest supplies the final returned key as the next cursor; an empty result
  terminates the scan. This fixed page bound fits every Draft control-message
  configuration without exposing a Host directory cursor.

There are no Handles, transactions, compare-and-set operation or recursive
namespace in v1. Those remain possible future extensions after concrete
cross-App and conflict-resolution requirements exist. Secrets remains reserved
because Storage v1 makes no confidentiality, device-key, backup or key-rotation
claim.
