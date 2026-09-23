# CompressCompare documentation

CompressCompare shows **where** a social-media site (or any upload pipeline) changed your image or video:
load the original and the copy you downloaded back, and the app draws a per-pixel heat map of the damage,
Lightroom-style, with the numbers (PSNR, SSIM, ΔE2000, JPEG quality, subsampling) next to it. This version
(0.3) is the port to macOS 12 Monterey or newer on Intel Macs; it also builds on Linux for development.

The documentation follows the [Diátaxis](https://diataxis.fr/) framework, so every page has one job.
Pick the column that matches what you are trying to do:

| I want to… | Go to |
|---|---|
| **learn** the app by doing something concrete | **Tutorials** – guided, start to finish, with the bundled samples |
| **get a specific job done** (build it, use the GPU, install ffmpeg, change a limit, add a site, export a heat map) | **How-to guides** – recipes that assume you know the basics |
| **look something up** (every config key, every key binding, what a metric means, the CLI) | **Reference** – dry, complete, alphabetical where it can be |
| **understand why** it works the way it does (the comparison pipeline, the coding rules, where the ideas come from) | **Explanation** – background and reasoning |

## Tutorials

1. [Your first comparison](tutorials/first-comparison.md) – from an empty window to a heat map you can read, in ten minutes, using `samples/`.
2. [Comparing a video](tutorials/video-comparison.md) – frame-by-frame comparison and "quality over time" plots.

## How-to guides

Building and installing

- [Build on macOS](how-to/build-macos.md) – `scripts/build-macos.sh`, the manual steps, troubleshooting
- [Build on Linux](how-to/build-linux.md) – development and CI, OpenCL tests with PoCL
- [Install ffmpeg](how-to/install-ffmpeg.md) – optional on macOS (WebM, MKV, FLV); needed on Linux for AVIF and HEIC

Using and customising

- [Use the GPU (OpenCL)](how-to/use-the-gpu.md) – check what is in use, the self-test, `cc_bench`
- [Load an image or video from a URL](how-to/load-from-a-url.md)
- [Change a setting or a limit with config.yaml](how-to/change-config.md)
- [Add or edit a site in the site database](how-to/add-a-site.md)
- [Export a heat map as a PNG](how-to/export-a-heat-map.md)

Developing

- [Run the tests and the static analysis](how-to/run-tests-and-static-analysis.md)
- [Generate the API documentation](how-to/generate-api-docs.md)

## Reference

- [Configuration keys (`config.yaml`)](reference/config.md) – generated from the source; every key, default and accepted range
- [Command line](reference/cli.md)
- [Keyboard shortcuts and mouse](reference/keyboard-shortcuts.md)
- [Metrics and statistics](reference/metrics.md)
- [Built-in site database](reference/sites.md)
- [Architecture and source layout](reference/architecture.md)
- API reference: run the `docs` target ([how-to](how-to/generate-api-docs.md)); every function has a docstring

## Explanation

- [How a comparison works](explanation/how-it-works.md) – resampling policy, comparison size, the heat-map pipeline, where the GPU comes in, accuracy caveats
- [Design principles: config.yaml, docstrings and the Power of 10](explanation/design-principles.md) – the coding rules the code base follows and where they were adapted
- [Where the ideas come from](explanation/references.md) – darktable, CLIJ2, Lightroom, and the papers behind the metrics
