# stb_image

`stb_image.h` v2.30, vendored unmodified from https://github.com/nothings/stb
(public domain / MIT dual licence -- see the header's tail). SND builds it
with `STBI_ONLY_PNG` + `STBI_NO_STDIO` inside `src/svg.cpp`; decoding is
exposed through `snd::ui::decodeImage` / `snd::ui::loadImageTexture`.
