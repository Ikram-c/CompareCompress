# Tutorial: your first comparison

In this tutorial you will load a picture and the copy Instagram would serve back, run a comparison, and learn
to read the heat map and the statistics. It takes about ten minutes and uses the synthetic files in
`samples/`, so you do not need an account anywhere.

You need a built app. If you do not have one yet, follow [Build on macOS](../how-to/build-macos.md) (one
command, `scripts/build-macos.sh`) or, for development, [Build on Linux](../how-to/build-linux.md).

## 1. Start the app with the samples

Open Terminal in the project folder and run:

```
open -a "$PWD/dist/CompressCompare.app" --args "$PWD/samples/original.png" "$PWD/samples/instagram.jpg"
```

`--args` passes the rest of the line to the app; the paths must be absolute because an app started with
`open` does not run in your current folder, which is what `$PWD/` takes care of. You can also start the
executable inside the bundle directly, which keeps its console output in Terminal:

```
dist/CompressCompare.app/Contents/MacOS/CompressCompare samples/original.png samples/instagram.jpg
```

(On Linux: `build/linux/compresscompare samples/original.png samples/instagram.jpg`.) If you would rather
use Finder, open the app, then drag `original.png` from the `samples` folder onto the **Original** card and
`instagram.jpg` onto **Site 1**.

Files given on the command line fill the slots in order: the first one becomes the **Original**, the next
ones become **Site 1**, **Site 2** and so on. Because *auto* is on (bottom of the left panel), the comparison
starts as soon as both files are loaded. Within a second the centre canvas shows the site's copy with an
orange-purple heat map blended over it, and the right panel fills with numbers.

You have just done the whole job. The rest of the tutorial is about reading the result.

## 2. Read the left panel

Each card in the left panel is a **slot**. The top card is the original (`PNG 1600x1200, 3.62 MB`); the second
is Site 1, whose status line reads something like `JPEG 1080x810, baseline JPEG, 4:2:0, q~75, 130 KB` (the
exact byte count depends on how the sample was copied; the pixels are what matter). That line is the first
result: the app decoded the JPEG headers and found that the file was re-encoded at
IJG quality 75 with 4:2:0 chroma subsampling, and that it was downscaled to 1080 pixels wide.

Hover the thumbnail of Site 1. A tooltip lists the same facts plus the quantiser estimates for luma and
chroma and which metadata blocks survived (`(metadata / colour profile stripped)` here).

Type `ig` in the **Specify site** box of Site 1 and press Return. The dropdown ranks Instagram first because
`ig` is one of its aliases; the card header now says *Site 1 - Instagram*. The site name is only a label for
you – the numbers come from the files, never from the name.

## 3. Read the heat map

Move the mouse over the picture. Three things happen:

- a tooltip shows the pixel under the cursor: its RGB values before and after, the difference per channel,
  the CIEDE2000 colour difference and the value of the current metric;
- the **corner loupe** (bottom right) magnifies the neighbourhood in three panes – *before*, *after*, *heat* –
  so you can see the JPEG blocks and ringing at pixel level;
- the heat map itself stays put: bright means "changed a lot", dark means "unchanged".

The colour scale is explained in the right panel under **Heat map**. The legend bar runs from 0 to the value
mapped to the brightest colour; with *auto scale* on, that value is the 99th percentile of the map, so a
handful of extreme pixels cannot wash out the picture. Hover the legend to read the value at any colour.

Now try the keys:

- `H` toggles the overlay on and off, so you can flip between the site's copy and the damage map.
- `Shift+H` shows the heat map alone.
- `[` and `]` change the overlay opacity.
- `M` cycles through the metrics. Watch how *Luma difference* barely reacts to the chroma subsampling
  while *Colour difference dE2000* lights up the saturated edges.
- `B` switches to the **block view**, which averages the map over the JPEG 8×8 grid. Blocks the encoder
  sacrificed stand out as uniform squares.

## 4. Compare side by side

Press `Y`. The canvas splits into **Before | After**. Press `Y` again to go back to the single (*Loupe*) view,
where `\` flips between before and after and holding `Space` peeks at the original.

Press `Shift+Y` for the **split** view: one picture with a draggable divider. Drag the line to move it, click
the round handle in the middle to rotate the divider from vertical to horizontal, double-click the line to
centre it.

Zoom with the mouse wheel or a two-finger scroll on the trackpad. The zoom is anchored under the cursor and snaps to *fit*, *100 %* and *200 %* on
the way. `Z` jumps between fit, 100 % and 200 %; `F` fits again. At 100 % and above the app switches to
nearest-neighbour filtering so every screen pixel is a real image pixel. Both panes stay linked.

## 5. Read the statistics

The **Statistics** section of the right panel has one row per fact, and every row has a tooltip that explains
it. For this pair you should see, roughly:

| Row | What it tells you |
|---|---|
| size `1600x1200 -> 1080x810` | the site downscaled the picture |
| file size `3.62 MB -> 130 KB (3%)` | and reduced the file to about a thirtieth |
| site JPEG `q~75, 4:2:0, baseline` | at quality 75 with chroma subsampling |
| PSNR `≈ 25 dB` | a low number because the test image is full of fine detail that does not survive downscaling |
| SSIM `≈ 0.85` | structure was clearly altered |
| dE2000 `≈ 3 mean / 60 max` | the average pixel moved by 3 ΔE – visible side by side |
| visible changes `≈ 33 %` | a third of the pixels moved by more than a just-noticeable difference |

Below the table is the **error histogram**: how the metric values are distributed. `log` switches it to a
logarithmic scale, which is usually easier to read because most pixels sit near zero.

## 6. Add a second site

Click **+ Add another site** and drop `samples/facebook.jpg` on the new card (or click the card and pick the
file). This copy was centre-cropped to a square, so the status bar reports *aspect ratio differs: original
centre-cropped to match* and the Statistics section gains four arrow buttons to nudge the crop window if the
edges look misaligned.

Press `N` for the **Survey** view: every site tiled next to the original, each with its PSNR / SSIM / ΔE line.
Click a tile to make it the active site, double-click to open it in the Loupe view. `←` and `→` also cycle
through sites.

## 7. Where to go next

- To compare something you actually uploaded, post the original, download it back and put the download in a
  site slot – or paste the direct image link: [Load an image or video from a URL](../how-to/load-from-a-url.md).
- To do the same with video, continue with [Comparing a video](video-comparison.md).
- Every key binding is listed in the [keyboard reference](../reference/keyboard-shortcuts.md); every number
  the app uses can be changed in [config.yaml](../how-to/change-config.md).
