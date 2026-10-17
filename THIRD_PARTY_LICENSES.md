# Third-party licenses

STS Lite itself is MIT licensed (see `LICENSE`). It builds on the works below.

## Stereo Triggered Sampler (STS) firmware

STS Lite is a reduced, unofficial clone of the Stereo Triggered Sampler (STS)
and is not affiliated with or endorsed by its authors or maker. Its playback
(varispeed resampling by four-point Hermite interpolation, and the left/right
average used to play a stereo file in mono) is based on the STS firmware,
in particular `src/resample.c`.

Source: https://github.com/4ms/STS

Only the STS software is used. The STS hardware design (CC BY-NC-SA 4.0) and
the STS sample files are not part of this project.

```
The MIT License (MIT)

Copyright (c) 2017 Dan Green (danngreen1@gmail.com) and Hugo Paris (hugoplho@gmail.com)

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
```

## Schwung plugin API header

`third_party/schwung/host/plugin_api_v1.h` is Schwung's plugin API header,
MIT License, Copyright (c) 2025-2026 Charles Vestal. The full text is in
`third_party/schwung/LICENSE` (Schwung's own license file, kept verbatim).

Source: https://github.com/charlesvestal/schwung

```
MIT License

Copyright (c) 2025-2026 Charles Vestal

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
```
