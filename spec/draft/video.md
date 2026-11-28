# Video 0.4

Video (service 24) decodes compressed video in the Host. Network access, container
demux and audio playback remain separate. No privileged permission is required to decode;
only the foreground component can open or use a decoder. Moving to the background,
stopping the component or closing its handle retires the decoder and all frames.
Hosts without a backend return count zero. Applications must handle this case.

`pxa_video.h` provides the C Guest API. Each capability entry describes one
implementation: codec 1 = H.264 Annex B, 2 = complete JPEG frames; implementation
1 = software, 2 = hardware. Multiple implementations of a codec may coexist.
Profile bits 1/2/4 denote Baseline/Main/High; zero for JPEG. max_level uses H.264
level_idc (40 = 4.0). Limits apply together: width, height, pixel count and maximum
compressed packet size. The recommendation describes a policy ceiling, without
promising a sustained frame rate. Applications choose streams using codec,
profile, level, dimensions and frame rate, not just a site's quality label.

CAPABILITIES takes an entry index u16 and returns the 40-byte record specified in
pxa-video.json. Index zero on an unsupported Host returns a zero entry with count
zero; other out-of-range indices return NOT_FOUND. OPEN takes eight bytes:
index,width,height,reserved (four LE u16). Dimensions reserve the upper bound for
the session. One decoder per service is supported; competing opens return BUSY.
The reply is a generation-tagged handle64. Core close releases it.

DECODE (I/O 256) takes a writable packet: pts_us:u64, flags:u32, reserved:u32,
then one compressed access unit. H.264 includes Annex B start codes and requires
SPS/PPS before slices; MP4 avcC and length-prefixed samples need demux/conversion.
Ordinary flags are zero. A drain packet has flags=1 and no compressed bytes.
Submission is synchronous; the input storage can be reused on return. Zero means
accepted, including packets producing no image. Errors invalidate the previous
frame. Decoder implementations must bound allocations before processing headers.

FRAME_INFO (I/O 257) fills exactly 32 bytes: width:u16,height:u16,stride:u32,
pts_us:u64,decode_us:u64,frame_bytes:u32,reserved:u32. WOULD_BLOCK means no frame
was produced. Pixels are tightly packed RGB565 little endian. READ (I/O 1) copies
successive bytes of the current frame and returns zero at its end. The next
DECODE or RESET invalidates it. Presentation follows returned PTS, including
reordered H.264 pictures. Drain repeatedly at end until FRAME_INFO would block.
RESET (I/O 258, empty payload) clears references and partial output; supply
SPS/PPS and restart at an IDR after seeking or changing stream.

Video 0.2 adds READ_SCALED (I/O 259): width:u16,height:u16,reserved:u32,
followed by exactly width×height×2 writable bytes. Dimensions must be nonzero, at most 4096 per side and
at most 4194304 output pixels. Upscaling and downscaling are both supported.
The Host copies a nearest-neighbor RGB565 resample directly into the packet after its eight-byte prefix, without
allocating an intermediate image or advancing READ's cursor. Return zero means
success; no frame returns WOULD_BLOCK. Video 0.1 Hosts return UNSUPPORTED;
Guests can fall back to READ and local scaling.

Video 0.3 adds CONFIGURE_SCALED (I/O 260), exactly width:u16,height:u16,
and READ_SCALED_PIXELS (I/O 261), exactly width×height×2 writable pixel bytes
without a prefix. The same dimension limits and sampling as READ_SCALED apply.
Configuration is per decoder, survives RESET, and is cleared on close. An invalid
configuration leaves the previous dimensions intact. Reading before configuration
returns BAD_STATE; without a frame it returns WOULD_BLOCK. Both operations return
zero on success. No pointer is retained and READ's cursor is unchanged.
This lets a Guest fill an acquired guest-mapped Surface buffer directly, then
present it without another frame copy. The Guest must follow Surface ownership
and pinned linear-memory requirements. Older Hosts return UNSUPPORTED, allowing
fallback to the prefixed read and ordinary Surface writes.

Video backends may support QUEUE_DECODE (I/O 262), with the same packet
format as DECODE. A successful queue copies its input before returning and
invalidates the previous frame. Only one decode may be outstanding. POLL_DECODE
(I/O 263, empty payload) returns WOULD_BLOCK while it runs, then consumes the
decode status. A successful result makes the frame available through FRAME_INFO
and the read operations. Poll returns NOT_FOUND when there is no queued result.
Implementations can post DECODE_READY (event 0x8001, decoder handle64 payload)
to wake the Guest. Guests may always poll. RESET and handle close retire any
pending work. A Host or decoder without this option returns UNSUPPORTED from
QUEUE_DECODE, so Guests can use synchronous DECODE.

POLL_FRAME (I/O 271) consumes the same queued completion and fills a 32-byte
FRAME_INFO buffer in one call. It returns 32 for a picture and zero when a
packet completed without a picture, including a completed drain. WOULD_BLOCK
leaves the request outstanding. A successful completion retires the request
once, whichever poll operation the Guest uses. An older Host returns
UNSUPPORTED and the Guest can use POLL_DECODE followed by FRAME_INFO.

The capability record's final u16 is `audio_codecs`, a companion audio bitmask.
Bit 0 enables AAC-LC ADTS decoding. Zero means no companion audio decoder.
AUDIO_DECODE (I/O 272) accepts pts_us:u64 followed by one ADTS frame, at most
8192 bytes including the timestamp. AUDIO_INFO (I/O 273) fills 24 bytes:
sample_rate:u32,channels:u16,bits_per_sample:u16,frame_bytes:u32,reserved:u32,
pts_us:u64. PCM is interleaved S16LE, mono or stereo, at most 16384 bytes.
AUDIO_READ (I/O 274) copies successive PCM bytes; WOULD_BLOCK means no decoded
audio, zero means the current frame is exhausted. Audio and video output views
are independent. RESET clears both. These operations do not access speakers:
applications submit PCM to the permission-gated Audio service for playback.

ESPP4-A uses OpenH264 2.7.0 with pinned BSD-2-Clause thread fixes for software
decoding of progressive 8-bit 4:2:0 Baseline/Main/High through Level 5.1, and esp_new_jpeg software MJPEG decoding.
The declared level does not raise the dimension, pixel, packet or memory limits;
small streams declaring a high level still undergo all SPS resource checks.
H.264 defaults to a recommendation of 640×360 at 5 fps, bounded by 1280×720
pixels, maximum side 1280. MJPEG is bounded by 1920×1080 pixels, maximum side
1920. Portrait streams use the same pixel limit. Host memory quotas can reject
an otherwise supported stream; applications must lower quality or report it.

## Bound Surface output

Create a profile-1 RGB565 Surface with HOST_PRODUCED (bit 7). BIND_SURFACE
(265) takes 16 little-endian bytes, the Surface handle64 followed by x, y,
width and height as u16. The nonempty destination rectangle must fit the
Surface. The owner must own both handles. A successful rebind discards any
unpublished frame. Binding sixteen zero bytes releases the output. On error
the previous binding remains usable.

After FRAME_INFO, PREPARE_SURFACE (266, empty buffer) scales the current
picture into a Host buffer and fills letterbox margins with black. A successful
prepare allows the decoder to reuse its input and picture storage for the next
packet. PRESENT_SURFACE (267, frame-id:u64, nonzero and increasing) publishes
the prepared buffer at the application's chosen PTS. Both return zero on
success. Retry WOULD_BLOCK on a later event or clock tick. RESET drops staged
output, retaining the binding. Closing either handle or losing foreground
retires its use. Existing layers, screen capture and trusted overlays use the
usual Surface presenter. Applications can fall back to mapped buffers or
scaled reads when creating or binding the output fails.

PRESENT_SURFACE_ORDERED (270) accepts the same frame ID as PRESENT_SURFACE.
It preserves presentation order for a bound producer. When a submitted
picture still awaits acquisition by the renderer, the call returns
WOULD_BLOCK, retains the prepared picture and leaves the frame ID available
for retry. Retry with the same ID on a later event or clock tick. Once the
renderer acquires the preceding picture, the producer can publish the next
one while the display scans the first. UNSUPPORTED permits an immediate
fallback to PRESENT_SURFACE with the same ID and prepared picture.

PREFERRED_SIZE (268) accepts the maximum image width and height as two
little-endian u16 values. It returns four bytes containing a supported aspect
fit within those bounds. It can be queried once before allocating a Surface.
UNSUPPORTED leaves the application free to use its original rectangle. The
application decides whether to accept the proposed dimensions.

PREPARE_AND_QUEUE (269) takes the same access-unit packet as QUEUE_DECODE.
A successful call stages the current picture and accepts the next packet.
The caller can immediately present the staged picture at its own PTS and
poll the queued decode separately. A backend can run both operations in
parallel when the current picture has an independent lifetime. WOULD_BLOCK
or UNSUPPORTED permits ordinary PREPARE_SURFACE followed by QUEUE_DECODE.
A closed Surface returns BAD_STATE and invalidates an in-flight handoff.
