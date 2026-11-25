# grok

Device-independent parts of the Launcher's Grok voice chat (`src/omarchy_grok.cpp`):

- `grok_face`: Grok's face, a black ball with two slanted eyes, drawn with smooth edges.
- `ws_frame`: WebSocket frame headers (RFC 6455).
- `byte_ring`: a fixed-size ring buffer for audio.

Copied unchanged from the grok-voice project (`~/projects/cardputer/grok-voice`, commit
0040067), where they are unit-tested on the laptop (`pio test -e native`) and the face can
be previewed (`pio run -e preview`). Change them there first, then copy them here.
