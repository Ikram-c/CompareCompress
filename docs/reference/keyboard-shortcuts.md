# Keyboard shortcuts and mouse

Shortcuts are single keys unless a modifier is shown, and they follow Adobe Lightroom Classic's compare
views where one exists. They are ignored while a text box has the keyboard focus (press `Esc` or click the
canvas first). `F1` shows the same list inside the app.

**On macOS, `Ctrl` means `Cmd`.** Dear ImGui swaps the two keys on macOS (`io.ConfigMacOSXBehaviors`, on by
default there), and the app's `Ctrl` shortcuts test ImGui's Ctrl modifier, so `Ctrl+O` is `Cmd+O`, `Ctrl+V`
is `Cmd+V` and so on. The menus, the status bar and the in-app shortcut list still print `Ctrl`. `Alt` is the
`Option` key. A Control-click (or a two-finger click on the trackpad) counts as a right-click. `F1` on a
MacBook keyboard is `fn+F1` unless the function keys are set to act as standard keys. The tables below give
both spellings where a modifier is involved.

## Views

| Key | Action |
|---|---|
| `E` | Loupe (single image) |
| `C` | Before/After Left/Right |
| `Y` | toggle Loupe ↔ Before/After Left/Right |
| `Alt+Y` (`Option+Y`) | toggle Loupe ↔ Before/After Top/Bottom |
| `Shift+Y` | toggle the Split variant of the current orientation (Left/Right ↔ Left/Right Split, Top/Bottom ↔ Top/Bottom Split) |
| `N` | Survey: every site at once (needs at least two site slots) |
| `\` | Loupe: flip Before ↔ After · split views: swap the two sides |
| `Space` (hold) | Loupe: show Before while held (Lightroom's *before* peek) |
| `←` / `→` | previous / next site |
| `Tab` | hide / show both side panels |
| `L` | corner loupe on / off |

The **Change view** button above the canvas cycles through the six layouts in the same order as the *View*
menu.

## Zoom and pan

| Input | Action |
|---|---|
| mouse wheel or two-finger scroll over the image | zoom about the cursor; steps of ×1.1 below 200 % and ×2 above (`view.zoom.step_below_2x` / `step_above_2x`), snapping to fit, 100 % and 200 % when a step crosses them |
| `Z`, middle click, double-click (not in split views) | cycle fit → 100 % → 200 % → fit (on a trackpad, which has no middle button, use `Z` or a double-click) |
| `F` | fit the whole image |
| `Ctrl` `+` / `Ctrl` `−`; on macOS `Cmd` `+` / `Cmd` `−` (also keypad) | zoom in / out one step about the view centre |
| left-drag | pan (starts after `view.pan_drag_threshold_px`) |
| **linked** / **unlinked** button | Link Focus: whether every pane shares the same zoom and position (on by default) |

Zoom is clamped between `min(view.zoom.min_fit_fraction × fit, 1)` (half of *fit*) and `view.zoom.max` (16×). At
100 % and above the textures are drawn with nearest-neighbour filtering so you see the real pixels.

## Split views

| Input | Action |
|---|---|
| drag the divider line | move the split |
| click the round handle in the middle of the line | rotate the split (vertical ↔ horizontal) at the pointer position |
| double-click the line | centre the split |
| **swap** button, `\` | swap the sides |

## Heat map

| Key | Action |
|---|---|
| `H` | overlay on / off (the *After* pane shows the site copy with the heat map blended over it, or plain) |
| `Shift+H` | heat map only on / off (the *After* pane shows just the colour-mapped map) |
| `[` / `]` | overlay intensity down / up by `heatmap.intensity_step` (0.05) |
| `M` / `Shift+M` | next / previous metric |
| `B` | block view: average over the JPEG 8×8 grid (`metrics.block_size_px`) |

The colour map, scale, gamma and cut-off have no keys; they are in the *Heat map* section of the right
panel.

## Files and windows

| Key | Action |
|---|---|
| `Ctrl+O` / `Cmd+O` | open a file into the next empty slot (where a native dialog is available) |
| `Ctrl+V` / `Cmd+V` | paste a URL or path from the clipboard into the hovered slot, or the next empty slot |
| `Ctrl+P`, `Ctrl+,` / `Cmd+P`, `Cmd+,` | Settings window |
| `F1` | Shortcuts & about window |
| window close (`Alt+F4` on Linux desktops that use it; `Cmd+Q` or the red close button on macOS) | quit |

Drag-and-drop works anywhere: dropping on a slot fills that slot, dropping elsewhere fills the Original
first and then the site slots in order, adding slots as needed.

## Slots (left panel)

| Input | Action |
|---|---|
| click the thumbnail area | open a file (native dialog) |
| right-click (two-finger click or Control-click on a Mac) the thumbnail area | *Open file…*, *Paste URL / path from clipboard*, *Clear* |
| hover the thumbnail | format, dimensions, JPEG quality and subsampling, quantiser estimates, metadata, decoder |
| type in *Specify site* | fuzzy search; `↑` `↓` choose, `Enter` accepts, `Esc` closes |
| *Fetch* / `Enter` in the URL box | download or load the typed URL / path |
| **Visualize compression** | run the comparison (with *auto* on it runs by itself whenever a slot changes) |

## Video

| Input | Action |
|---|---|
| timeline slider (*Video* section) | choose the timestamp; frames are pulled when you release it |
| **Sample quality over time** | compare `ffmpeg.sample_points` evenly spaced frames and plot PSNR and SSIM |
| `<< 1 s`, `< frame`, `frame >`, `1 s >>` | step the timeline by one second or one frame |

Keyboard and gamepad navigation (Dear ImGui's standard mappings) are enabled.
