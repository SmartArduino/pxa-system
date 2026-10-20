# PXA Audio Draft 0.11

Audio defines a permission-bound session and an atomic, App-owned playback
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

The simulator sends PCM and generated tones to its SDL device. The legacy ESP session
accepts 16 kHz mono 20 ms frames and routes up to three concurrent PXA sessions
to independent Game mixer inputs; it cannot address master gain, voice or other
Apps' streams.


## Exact source PCM (minor 10)

`open-pcm` (opcode 5) uses wire profile 1 and a nonzero request token. It
requires the same `audio.playback` / `media` permission and session quotas as
`open-session`. The fixed 16-byte request contains a permission Handle (`u64`),
source sample rate (`u32`, 8000..48000), channel count (`u8`, 1 or 2), and three
zero reserved bytes. PCM is interleaved S16LE. An unsupported source format
returns `unsupported`; the Host never substitutes another source format.

The success result is 56 bytes including status: the existing session/format
records 3..6, then tag 7 sink rate (`u32`), tag 8 sink channels (`u8`), and tag 9
queue capacity in source frames (`u32`). Source rate and channels echo the
request exactly; frame duration is 20 ms. Maximum WRITE size is
`floor(source_rate * 20 / 1000) * channels * 2` bytes. Each write is atomic;
`would-block` changes neither queued samples nor resampler phase. A main-loop
Guest must cooperate through its event port before retrying. It must not busy
spin or recursively enter another Guest callback.

A PCM session supports WRITE, graph commit, query, flush and close. Tone,
effect, prepared sound, asset and music I/O return `unsupported`. Its counters
measure source sample frames, independent of channel count. The queue capacity
is bounded to 1..`floor(source_rate * 120 / 1000)` source frames. `accepted`
counts renderer consumption unless the sink-submitted flag is set; neither
value guarantees that physical hardware has played the samples.

The sink describes the actual route. A mono speaker can accept stereo source
PCM and combine both channels on the Host; this does not imply stereo hardware.
The ESP pull provider rejects rates above its sink rate, uses continuous linear
interpolation for upsampling and symmetric stereo averaging, and places its
fixed 24,144-byte mixer workspace in charged external memory. Existing boards
retain 16 kHz unless configured otherwise. A 48 kHz device still renders old
prepared sounds and streamed music on their 16 kHz source clock.

C provides `pxa_audio_open_pcm` / `pxa_audio_parse_pcm`; C++ provides
`AudioService::open_pcm(permission, rate, channels)` and `AudioSession::pcm_sink()`.
The C++ parser checks the source echo and closes a newly allocated Handle on
malformed results or abandoned requests. Old Guests can keep using
`open-session` and its unchanged 35-byte result.

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


## Synthesized effect groups (0.9)

`PLAY_EFFECT` (`0x107`) submits `u8 layer_count`, `u8 track`, two zero
reserved bytes, followed by 1..8 existing 14-byte tone descriptors. All layers
share a starting sample, and their delays are relative to that sample. The
entire group is validated and accepted atomically; an invalid layer or exhausted
budget changes nothing. `WOULD_BLOCK` retains no pending event.

Track 255 creates an independent effect. Tracks 0..5 atomically replace the
previous synthesized effect on that track, including at full capacity. These
tracks are separate from prepared PCM asset sound tracks. `STOP_EFFECT`
(`0x108`) takes a track byte and three zero reserved bytes; stopping an inactive
track succeeds. Flush/close cancel all groups. Suspension pauses their clock.
Already submitted physical output cannot be retroactively replaced or stopped.

PCM and legacy `PLAY_TONE` retain ordered FIFO semantics. Effect groups use
otherwise free FIFO slots and run alongside that FIFO. ESP and SDL currently
share four slots per session, with three sessions, without another allocation.
The maximum number of named tracks is not a guarantee of six simultaneous
slots. Applications should handle admission errors and choose their own
replacement categories rather than retrying obsolete game events.

For state accounting, one group submits its longest layer extent, including
delay, as one timeline; `queued_samples` sums remaining timelines (PCM FIFO
and parallel groups), not estimated wall time to silence. Replaced/cancelled
samples leave the queue without increasing `accepted_samples`. Accepted still
means renderer/sink submission, not speaker onset. Oscillator phase and
pseudo-random white noise are position-derived, so output block size and sink
retry do not change the waveform. Noise uses a deterministic integer hash;
legacy FIFO tones retain their previous noise generator.

C++: `session.effect(tones)` plays independently,
`session.effect(tones, EffectTrack::track0)` replaces track zero, and
`session.stop_effect(EffectTrack::track0)` stops it. Prepared assets use the
distinct `SoundTrack` type; integers and the other track type are not accepted.
An explicit cast of an invalid track returns `invalid_argument` without I/O.
`AudioSession::max_effect_layers`, `effect_track_count`, and `sound_track_count`
describe protocol limits, not available concurrent capacity.
The span is borrowed only during the call;
the Host copies compact descriptors before returning. No hot-path allocation
or decoding is required.

## Synchronous PCM accumulation (0.11)

`MIX_PCM` (`0x109`) is available on an existing Audio session, including a PCM
session. It performs bounded computation in the calling Guest's linear memory.
It does not play, enqueue, clear the output, advance counters or change the
session clock. The Guest retains scheduling, loop splitting and final output
quantization; the result may subsequently feed the ordinary PCM WRITE path.
This is an intermediate migration path for engines with existing mix buffers.

The 16-byte little-endian header is `target_offset:u32`, `target_frames:u32`,
`spans_offset:u32`, `span_count:u16`, `reserved:u16`. The target is interleaved
signed S32LE stereo, aligned to four bytes. Each 32-byte span contains:

| Offset | Field |
| --- | --- |
| 0 | source offset, u32 |
| 4 | source bytes, u32 |
| 8 | initial source-frame position, unsigned Q16 u32 |
| 12 | source-frame step, unsigned Q16 u32 |
| 16 | first destination frame, u32 |
| 20 | destination frame count, u32 |
| 24, 26 | left/right linear gain, u16 Q8, 0..256 |
| 28 | encoding: S8=1, U8=2, S16LE=3 |
| 29 | source channels: 1 or 2 |
| 30 | reserved u16, zero |

The Host selects source frame `floor((position + n * step) / 65536)`. Mono
feeds both sides; stereo preserves left/right assignment. S8 and U8 expand to
signed 16-bit magnitude before multiplying by gain and dividing by 256 toward
zero. Contributions add in descriptor order, saturating only at signed
32-bit accumulator limits. There is no automatic fade, smoothing or S16
clipping. In particular, S8 gain 248 produces exactly `signed_sample * 248`.

Limits: 1..32 spans, 1..4096 target frames, sum of span frame counts at most
16384, step 1..1048576. Last accessed Q16 position must fit u32, source bytes
must contain complete source frames, and all accessed frames must fit their
declared source and target extents. Reserved fields must be zero. The entire
target extent must be disjoint from the header, descriptors and every declared
source range. Source ranges may overlap each other.

All validation precedes all target writes. Malformed input returns
`INVALID_ARGUMENT`; exceeding work/count/target-frame limits returns
`LIMIT_EXCEEDED`. Missing serialized linear-memory dispatch returns `BAD_STATE`;
older Audio services return `UNSUPPORTED` for the operation. Errors leave the
target unchanged. Success returns exactly 16. No source or target pointer
survives return, and the Host must not re-enter the Guest during processing.
The portable Host implementation allocates no heap memory and stores no
additional per-session state.

C: encode `pxa_audio_mix_span_t` entries with `pxa_audio_encode_mix_span`, then
call `pxa_audio_mix_pcm` with their wasm32 offset. C++: `AudioMixSpan::encode`
and `AudioSession::mix_pcm` expose the same explicit offsets. SDK wrappers
check result length; a successful short response is a protocol error.
