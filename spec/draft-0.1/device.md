# PXA Device Draft 0.1

Device v1 is service 15. It exposes selected physical interface identifiers
through explicit, exact-scope `device.identity` Permission Handles. It does
not define a universal device MAC: every request names one interface and one
identity form.

## GET_MAC

`get-mac` has a nonzero request ID. Its request records, in ascending tag
order, are `mac-kind:u16` (tag 1) and `permission-handle:u32` (tag 2). The
permission scope must exactly match the requested kind:

| Kind | Scope |
| ---: | --- |
| 1 | `mac.wifi.station.hardware` |
| 2 | `mac.wifi.softap.hardware` |
| 3 | `mac.bluetooth.hardware` |
| 4 | `mac.ethernet.hardware` |
| 5 | `mac.wifi.station.current` |

The successful result starts with the Core status and then contains `mac-kind`
(tag 1), raw six-byte `mac` (tag 2), and `flags:u32` (tag 3). Guests must not
assume text formatting; they choose the representation needed by their
protocol. A Host returns `not-found` when the requested interface or identity
form is unavailable.

Flag bit 0 marks a hardware address, bit 1 marks a current runtime address,
and bit 2 marks a locally administered address. No identifier is a secret, but
the permission is still required because MAC addresses are stable identifiers.
