# PXA Audio Draft 0.3

Audio 0.3 defines a permission-bound session and an atomic, App-owned playback
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

The simulator sends PCM and generated tones to its SDL device. The ESP provider
accepts 16 kHz mono 20 ms frames and routes up to three concurrent PXA sessions
to independent Game mixer inputs; it cannot address master gain, voice or other
Apps' streams.
