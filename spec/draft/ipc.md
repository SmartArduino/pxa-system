# PXA IPC Draft 0.1

IPC v1 is service 7. This initial protocol brokers asynchronous request/reply
messages between Components in one activated, signed App. It does not expose
shared linear memory, nested Guest calls, inter-App routing, endpoint discovery,
streams, timeouts or cross-App ACLs. Those require signed Package endpoint
metadata and a separate access-policy review.

The Core v1 preview accepts the existing IPC call/reply record payloads with
full 64-bit request tokens. Their normal completions restore the exact Guest
token. The broker's `request` and `result` notifications remain keyed by its
opaque 32-bit call ID; the WAMR adapter zero-extends that ID into the v1 event
envelope token field without consuming a pending-request mapping. A Guest must
not treat notification call IDs as request tokens. `pxa_ipc.h` provides
separate typed decoders for both kinds of event.

The Host registers an endpoint name to one running provider Component. An
endpoint is 1 to 64 ASCII bytes: it starts with a letter and later bytes may be
letters, digits, `.`, `_` or `-`. A name is unique in the active App.

## Calls and replies

`call` has one endpoint record (tag 1) and an optional opaque payload record
(tag 2). Its normal Core completion carries a nonzero `call_id:u32`. The Broker
then posts a reliable `request` event (`opcode 0x8001`) to the provider with
that call ID as the event request ID and the original records as payload.

The provider answers with `reply`: tag 1 is the call ID, tag 2 is a known Core
`status:i32`, and tag 3 is an optional opaque result payload. The Broker posts
a reliable `result` event (`opcode 0x8002`) to the original caller. The result
payload starts with `status:i32`; on successful status it may contain one tag 3
payload record.

The Broker only accepts a reply from the registered target Component. A missing
endpoint returns `not-found`; a full pending-call table returns
`resource-limit`; a full reliable provider or caller mailbox returns
`would-block` without silently accepting or dropping the message. Call IDs are
opaque and valid only while the call is pending.

The Broker reserves the Core completion before forwarding a call or posting a
reply. If that reservation fails, the control import rejects the request and
does not notify the other Component. A lazy call waiting for its provider can
still be cancelled; the Broker releases its copied payload before attempting
endpoint resolution again.

When a provider stops, every pending call to it is removed and its surviving
caller receives a `cancelled` result event when that event can be admitted.
When a caller stops, its pending calls are discarded. This preserves the Core
rule that no Guest callback is entered synchronously by another Guest import.
