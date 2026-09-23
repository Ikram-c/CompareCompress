# Tutorial: comparing a video

This tutorial compares a short clip with a heavily transcoded copy of it, frame by frame, and then plots the
quality over the length of the clip. It assumes you have done [Your first comparison](first-comparison.md).

On macOS nothing else is needed: the app reads MP4 and MOV files through OpenCV's AVFoundation back-end,
which uses the Mac's own (hardware) H.264 and HEVC decoders. On Linux the app uses OpenCV's FFmpeg back-end.
The `ffmpeg` and `ffprobe` programs are only a fallback for containers those back-ends cannot open, such as
WebM or MKV ([how to install them](../how-to/install-ffmpeg.md)); the bundled samples are MP4 and do not need
them.

## 1. Check that videos can be opened

Start the app with no files (`open dist/CompressCompare.app`, or double-click it).

If the status bar at the bottom says *no video back-end - videos are disabled (see Settings)*, OpenCV was
built without a video reader and ffmpeg was not found either; the *Settings* window (`Cmd+P` on macOS,
`Ctrl+P` on Linux) shows what was looked for. On a Mac built with `scripts/build-macos.sh` this does not
happen: the Settings line *Video: AVFOUNDATION* confirms the back-end. Otherwise the status bar shows the
usual hint text and you are ready.

## 2. Load the two clips

Drop `samples/original.mp4` on the **Original** card and `samples/tiktok.mp4` on **Site 1** (or start the
app with both paths on the command line). For each file the app opens the video, shows the codec, size,
frame rate, duration and bit rate in the status line of the card (for example
`h264 640x360 @ 25 fps, 4.0 s, 1500 kb/s`; through OpenCV the bit rate is the file's average, its size
divided by its duration), and decodes the frame at time 0. A `VIDEO` badge appears on the thumbnail, and its tooltip names the back-end that decoded it, for
example *decoded with OpenCV AVFOUNDATION*.

The comparison runs on those two frames exactly as it does for still images: the original frame is
resampled to the site's frame size and the heat map and statistics are computed. The Statistics section
gains three rows – *video* (codec and bit rate of both files), *frame rate* and *frames compared* (the two
timestamps).

## 3. Scrub through the clip

A **Video** section appears at the bottom of the right panel. Drag the timeline slider and release it: both
slots fetch the frame at the new timestamp and the comparison recomputes. The buttons step by one frame
(`< frame`, `frame >`) or by one second. Each step seeks the open video to the new time and decodes one
frame; nothing is kept in memory but the current frames. (With the `ffmpeg` fallback, each step is a
separate `ffmpeg -ss T -i file -frames:v 1` run and costs a few hundred milliseconds.)

Watch the heat map change from frame to frame: the transcoder spends its bits unevenly, so a static shot may
look clean while a cut or a fast pan lights up.

## 4. Sample the quality over time

Set the **points** slider to 8 and press **Sample quality over time**. The app decodes eight evenly spaced
frames from both clips (at the same *relative* position, because sites sometimes trim a video) and computes
PSNR and SSIM for each pair. Two plots appear: PSNR in dB and SSIM from 0 to 1. Hover a plot to read a sample;
click it to jump the timeline there and look at that frame.

## 5. Keep the limits in mind

Videos are refused when they exceed the caps in the *Settings* window: file size (250 MB by default),
duration (10 minutes) and longer edge (3840 px). The caps exist because frames are decoded at full
resolution; raise them in Settings for one session, or permanently in `config.yaml` (`limits.video.*`, see
[Change a setting](../how-to/change-config.md)).

Rotated phone videos are handled: the app reads the display rotation from the MP4/MOV track header and turns
every frame upright when the back-end does not do so itself, so a portrait clip is compared in portrait on
every back-end.

## 6. Where to go next

- The Statistics tooltips explain what PSNR and SSIM ranges mean for video; the
  [metrics reference](../reference/metrics.md) has the definitions.
- `acceleration.video_backend` in [config.yaml](../reference/config.md) chooses between OpenCV and the
  `ffmpeg` executable if you want to compare the two.
- HEIC photos from an iPhone open like any other still on macOS (through ImageIO); on Linux they, and AVIF
  files, go through ffmpeg. Nothing changes in the workflow.
