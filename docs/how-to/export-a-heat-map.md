# How to export a heat map as a PNG

The app writes the colour-mapped heat map of the **active site** – what you see in the *Heat* view, without
the photo under it – as an 8-bit RGB PNG at the comparison size.

## Steps

1. Run a comparison and select the site you want (the site selector above the canvas, or the `←` / `→`
   keys). The *Heat map* section of the right panel shows the legend for it.
2. Set the map up the way you want it in the file:
   - **metric** (`M` cycles; or the dropdown) – ΔE2000 is the most readable, 1−SSIM shows blur and ringing;
   - **colour map**;
   - **scale**: *auto* maps the 99th percentile (`metrics.auto_scale_percentile`) to the top colour, or untick
     it and set the maximum yourself so that two exports use the same scale and can be compared;
   - **gamma** (curve of the ramp) and **block view** (`B`, averages over the 8×8 JPEG grid).

   Overlay intensity, cut-off and the *overlay / heat only* mode do **not** affect the export; they only
   change how the map is blended over the picture on screen.
3. Choose **File → Export heat map as PNG…** or press the small **export PNG** button under the legend.
4. A native save dialog proposes `heatmap_<site>.png` (`heatmap_instagram.png`; characters other than
   letters and digits become `_`). Confirm; the status bar says *saved …* with the path.

On Linux without `zenity` or `kdialog` (and in any build where no dialog helper is found) there is no dialog:
the file is written under the proposed name in the current working directory.

## What is in the file

- Size: the comparison size – the site's output size by default, the original's when *compare at site size*
  is off ([how a comparison works](../explanation/how-it-works.md)).
- Pixel value: `colormap(clamp(value / max, 0, 1) ^ gamma)`, exactly the shader's formula, where `max` is
  the auto or manual scale and `value` the metric at that pixel (or the 8×8 block mean in block view).
- No alpha, no metadata. The metric, scale and colour map are not recorded in the file – note them yourself,
  or use a manual scale so the mapping is reproducible.

## Sharing a scale between exports

To compare two sites in one figure, use the same scale for both: untick *auto*, type the same maximum for
each (the legend shows the current auto value to start from, in the metric's unit), export each site.
With *auto* on, each export would be stretched to its own 99th percentile and identical colours would mean
different amounts of error.

## Related

- [Keyboard shortcuts](../reference/keyboard-shortcuts.md) – `M`, `B`, `[` `]`, `H`.
- [Metrics](../reference/metrics.md) – what each metric measures and its unit.
- [config.yaml](change-config.md) – `heatmap.*` keys set the defaults for all of the above.
