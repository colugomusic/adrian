# adrian

## adrian::chain
`#include <adrian-chain.hpp`

This is a buffer that can be used for reading/writing audio data. I use this to implement Blockhead's send/receive buffers and the catch buffer (below.)
- It can be really big.
- The memory for the chain is not allocated immediately, unless `adrian::chain_options::allocate_now == true`. Instead, it's allocated in a background thread.
- There is built-in a mechanism for reporting allocation progress back to the UI thread.
- The chain is split up into smaller buffers of `adrian::detail::BUFFER_SIZE` (16384) bytes. (About 0.4ms at a sample rate of 44100hz). The chain is allocated one chunk at a time.
- When a chain is shrunk or erased, unused sub-buffers are returned to a pool for reuse.
- The buffer has a built-in [mipmap](https://github.com/colugomusic/ads/blob/master/include/ads/ads-mipmap.hpp), if `adrian::chain_options::enable_mipmaps == true`, which can be used for nice waveform rendering.

## adrian::catch_buffer
`#include <adrian-catch-buffer.hpp`

This is the circular buffer which powers Blockhead's audio input system. You can simultaneously play back part of the buffer while writing to it in the audio thread and also read from it in another (non-realtime) background thread (e.g. to grab parts of the buffer and generate samples from it) without causing any interruption to the playback or recording. All the horrible nightmare of doing this in a realtime-safe manner is encapsulated by this class.
