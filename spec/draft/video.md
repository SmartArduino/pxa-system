# Video 0.3

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
