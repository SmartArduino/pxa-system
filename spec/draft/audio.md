# PXA Audio Draft 0.8

Audio 0.8 defines a permission-bound session and an atomic, App-owned playback
graph control plane. `open-session` requires an `audio.playback` Permission
Handle whose exact scope is `media`. It returns a Component-owned audio-graph
Handle plus the Host-selected PCM format. v1 currently defines only the media
usage and speaker route.

`commit-graph` accepts a complete replacement snapshot: its session Handle,
gain in signed Q8 dB, zero through five parametric EQ bands, and the speaker
route. The Host validates all records before passing the immutable snapshot to
its audio engine. A Guest can only commit a Handle it owns; closing or revoking
the underlying permission closes the session. Apps cannot address master gain,
other Apps' sessions, hardware codecs or raw I2S endpoints.

Audio service minor 1 adds playback PCM submission through the session Handle:
call `pxa_io(handle, PXA_IO_WRITE, pcm, size)` after a successful graph commit.
PCM is signed 16-bit little-endian and interleaved by channel. A write contains
at most one Host-negotiated frame and either returns its complete byte count or
an error; `would-block` means the bounded Host queue is full and the Guest must
retry from a later clock event rather than spin in the current callback.

The Host audio thread must never enter Wasm. The Host copies PCM out of Guest
memory before queuing it to its renderer. Encoded framing, microphone capture,
decoder/encoder selection and direct hardware endpoints remain outside this
minor. Hosts own resampling, mixing and focus policy.

Audio service minor 3 adds `play-tone` on the session resource. Its fixed
8-byte command describes one bounded procedural tone. The Host copies only the
descriptor and generates PCM on its audio task, so a slow render callback
cannot starve an already accepted effect. This is intended for short UI and
game effects; PCM write remains the compatibility and streaming path.

Audio minor 4 adds bounded `play-asset` and `control-asset` operations on the
same session Handle. Minor 5 adds `query-state` and `flush`, returning submitted
and accepted sample counters, queue depth and a sink-submitted flag. The
Host owns decoder resources and validates asset paths before playback.

Under `pxa.core.v1`, `open-session` request tag 1 carries a native 64-bit
Permission Handle; its success result tag 3 carries a native 64-bit Session
Handle. `commit-graph`, `query-state` and `flush` request tag 1 carry that
same full-width Session Handle. The Host rejects the four-byte v0 forms for
Core v1 Components, checks the full generation for every operation and
revokes the session when its Permission authority is revoked. PCM, tone and
asset I/O use `pxa_io(handle:u64, ...)`. The freestanding
`pxa_audio.h` offers one-buffer request builders and typed result parsers.

The simulator sends PCM and generated tones to its SDL device. The ESP provider
accepts 16 kHz mono 20 ms frames and routes up to three concurrent PXA sessions
to independent Game mixer inputs; it cannot address master gain, voice or other
Apps' streams.


## Current 16 kHz product profile

ESP and desktop share an allocation-free PCM/tone mixer with three voices,
four FIFO commands per voice, saturating output, and peaking EQ below Nyquist.
A tone advances incrementally on the same sample clock as PCM from other
voices. A full FIFO returns `would-block` without consuming input. A flush
always clears the selected voice, including when its FIFO is full; already
mixed device buffers need not be individually retractable. Suspend preserves
PCM/tone position. Query counters cover PCM/tone input, not asset decoding.

The asset bus is separate: one streaming music owner and six short effect
voices, with pause/resume/stop scoped to the session. Asset gains come from
play/control-asset; commit-graph gain/EQ currently apply to the PCM/tone bus.
Accepted asset commands do not imply successful decode or playback completion;
minor 7 provides instance-specific music state notifications.

Both products preload U8 mono 16 kHz `.pcm` effects of 1..16,000 bytes through
Assets 1.3, then play their handles without file I/O or a second PCM cache.
They stream Ogg music through an 8,192-sample PCM ring. Both ESP and desktop decode
Vorbis/Opus. Use mono 16 kHz Vorbis for shared
assets. MP3, WAV, capture and arbitrary decoder selection are not exposed.

## Prepared sound I/O (minor 6)

`play-sound` (`0x103`) takes exactly 12 little-endian bytes: an Assets audio
handle (`u64`), gain (`i16`, -60..0 dB in Q8), and two zero reserved bytes.
The session must be open and committed; the sound must belong to the calling
Component and identify a ready PCM object. Wrong type/owner/generation is
rejected before the backend sees the object. Accepted I/O returns 12; voice
capacity exhaustion returns `would-block`, consuming no reference.

Use `pxa_assets_load_sound(token, path)`, parse its LOAD result, then call
`pxa_audio_play_sound(session, handle, gain)`. Accepted voices hold independent
references: closing the Guest handle does not stop playback. Natural end or
session stop drops those references; the shared resource worker reclaims an
evictable object outside the mixer lock. No heap allocation or final free occurs
on the prepared playback path. PCM payload never needs to enter Guest memory.

The product file-play path now accepts Ogg only. Apps using `play-file` for PCM
must migrate to Assets loading and handle playback, declare Assets 1.3 and
Audio 0.6, and rebuild their package. There is no implicit synchronous fallback.
The asset bus still does not pass through PCM/tone graph EQ.

## Music instances and notifications (minor 7)

`play-music` (`0x104`) prepends an eight-byte zero output slot to the existing
`play-asset` path/gain/flags command. On acceptance the Host writes a nonzero
`u64` playback instance into that slot and returns the complete command length.
`pxa_audio_play_music(session, path, loop, gain, &instance)` builds the command
in a bounded 528-byte buffer; paths contain 1..511 bytes. Negative return values
mean rejection and leave the current playback unchanged. Acceptance may still
be followed by asynchronous open/decode failure. Looping music does not emit
an ENDED event at each loop boundary.

Event `0x8001` has request token zero and exactly 24 little-endian payload bytes:

| Offset | Type | Meaning |
| --- | --- | --- |
| 0 | u64 | Full Guest session handle |
| 8 | u64 | Playback instance from the accepted command |
| 16 | u8 | READY=1, ENDED=2, STOPPED=3, REPLACED=4, ERROR=5 |
| 17 | 3 bytes | Zero reserved bytes |
| 20 | i32 | Negative status for ERROR; zero otherwise |

The current desktop and ESP profiles share the native `pxa_audio_buffer`
state machine: an 8192-sample mono S16 ring starts after 4096 samples are
prepared (256 ms of audio at 16 kHz). This is a data threshold, not a fixed
startup sleep. An EOF tail can start below the threshold. Starvation counts
once per episode, emits the available samples, then waits for the threshold
or final EOF tail before resuming. Recovery does not emit another READY.
Paused output and natural EOF do not count as underruns. These are Host
buffering policies; no new per-sample or per-refill Guest events are added.

Use `pxa_audio_parse_playback` and compare both session and instance. READY
means PCM has been prepared for output. Each accepted instance can produce
one READY and one terminal event; failure or replacement before preparation
can produce only the terminal. READY already recorded before termination is
delivered first. Terminal reasons are mutually exclusive. Closing a session
revokes its pending notifications rather than requiring the Guest to keep a
closed session alive to drain them.

Host backends retain these records in a fixed eight-instance mailbox. Core
event-pool pressure delays delivery without dropping accepted records; full
mailboxes reject new plays with `would-block`. Runtime-thread polling transfers
records into Core; audio threads only update the mailbox and signal a wakeup.
This is separate from async request completion reservations. The legacy Ogg
`play-asset` wrapper uses the same engine but does not return the instance;
applications needing correlated state should use `play-music`.

ENDED waits for the prepared ring to drain; ESP also waits for its in-flight
output callback to return. This is a Host output boundary, not proof that a
remote codec, SDL hardware buffer or DAC has physically drained. Device output
failure is ERROR, not successful ENDED. Sample-accurate hardware drain remains
outside the current backend callback contract. Music integrity-before-consume,
shared-storage underrun tests and full decoder memory accounting remain part
of the resource-refactor acceptance work.

## Prepared sound tracks and music-only control (minor 8)

The existing twelve-byte `play-sound` command keeps its append-one-shot behavior.
The sixteen-byte form contains asset handle (u64), gain (i16 Q8 dB), track (u8,
0..5), loop (u8, 0/1), and four zero reserved bytes. A successful command replaces
the existing voice owned by that session and track, or uses an available voice.
Admission failure leaves the existing voice unchanged. The Host pins the new
asset before releasing the replaced pin. Loops and fades advance on the Host
sampling clock, without Guest timers or PCM pumping.

`control-sound` (0x105) is eight bytes: track, action, gain (i16), four reserved
zero bytes. Actions reuse PAUSE=1, RESUME=2, STOP=3, SET_GAIN=4; non-gain commands
require zero gain. It affects only that session's named track. Missing tracks
are an idempotent no-op. Stops and replacement tails fade over at most 64 samples
(4 ms at 16 kHz); paused stops release immediately. Gain changes ramp per sample.
The ESP and SDL implementations share the resident voice renderer and a soft
output limiter (unchanged below 28800, smooth saturation above), with six
concurrent sound voices globally; named tracks do not reserve extra capacity.

`control-music` (0x106) reuses the four-byte control-asset payload but affects
only streamed music. Legacy control-asset retains its whole-session asset
behavior. Music ducking therefore cannot overwrite prepared-sound gains.

Assets encoding 9 identifies raw signed 16-bit little-endian mono 16 kHz PCM
(`.s16`), with even, nonzero stored/decoded length and a 960000-byte ceiling
(30 seconds). Load goes through the authenticated Assets worker/cache and its
memory budgets. Encoding 5 unsigned eight-bit PCM remains limited to one second.
Long loops may need a larger bounded asset-cache ceiling; no allocation, file
read or codec processing occurs on prepared-voice rendering. Sound commands
return admission status; unlike music, they do not emit readiness/end events.
