# Third-party code

| Directory | Project | Version | Licence | Changes |
|---|---|---|---|---|
| `minimp3/` | [minimp3](https://github.com/lieff/minimp3) by lieff — MP3 decoder | commit `afb604c` | CC0 1.0 (public domain), see `minimp3/LICENSE` | `MINIMP3_SCRATCH`: the ~16 KB scratch structure can be provided by the caller instead of living on the stack (ESP32 task stacks are in internal RAM). Marked "Kronograf patch". |
| `tjpgd/` | [TJpgDec](http://elm-chan.org/fsw/tjpgd/) by ChaN — JPEG decoder | R0.03 | ChaN's free licence (no restriction on use; keep the notice), in `tjpgd.c` | `tjpgdcnf.h`: `JD_FORMAT` 1 (RGB565 output), `JD_FASTDECODE` 1 (32-bit CPUs). |
