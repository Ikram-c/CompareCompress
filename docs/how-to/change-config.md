# How to change a setting or a limit with config.yaml

Every number the app uses – window size, download caps, SSIM window, heat-map defaults, time-outs, worker
threads, the site database – lives in one YAML file, `config/config.yaml`. A copy of it is compiled into the
executable, so the app runs without any file. To change something, you write a **second** file that contains
only the keys you want to override.

## 1. Decide where the override lives

The app looks, in this order, and uses the first one it finds:

1. the file named with `--config <file>` on the command line;
2. `config.yaml` **next to the executable** (on Linux this is where `cmake --install` puts the editable
   copy; on macOS it would be inside `CompressCompare.app/Contents/MacOS/`, which a rebuild replaces);
3. the **per-user file** – the place for your settings:
   - macOS: `~/Library/Application Support/CompressCompare/config.yaml`;
   - Linux: `$XDG_CONFIG_HOME/compresscompare/config.yaml`, which is
     `~/.config/compresscompare/config.yaml` when `XDG_CONFIG_HOME` is not set;
4. `config.yaml` in the **current working directory** (useful from a terminal; an app started from Finder
   has no meaningful working directory).

Only one override is read; they do not stack. When the app starts with an override it prints
`configuration: <path>` on the console (start it from a terminal to see it; on macOS run
`CompressCompare.app/Contents/MacOS/CompressCompare` from Terminal).

The easiest way to get a per-user file is the *Settings* window (`Cmd+P` on macOS, `Ctrl+P` on Linux): it
shows *settings file: <path>*, and **edit config.yaml...** creates that file if it does not exist yet and
opens it – in TextEdit on macOS (`open -t`), with the desktop's default editor on Linux (`xdg-open`). A new
file starts with a two-line comment followed by the complete built-in configuration, so every key is there
to change. Changes apply the next time the app starts.

## 2. Write only the keys you change

An override file has the same layout as `config/config.yaml`. It may contain every key – that is what
**edit config.yaml...** writes – but it only needs the ones you change; you can delete the rest.
Maps are merged key by key; scalars and lists replace the built-in value. Example – allow bigger downloads,
default to the Viridis colour map and make tooltips appear faster:

```yaml
schema_version: 1        # optional, but if present it must be 1

limits:
  max_download_bytes: 524288000   # 500 MiB
  video:
    max_bytes: 1073741824          # 1 GiB

heatmap:
  colormap: viridis

app:
  tooltip:
    hover_delay_short_s: 0.1
```

Everything not mentioned (`limits.max_image_bytes`, `heatmap.metric`, …) keeps its built-in value.

The [configuration reference](../reference/config.md) lists every key with its default, its type and the
range the app accepts. `config/config.yaml` itself has a comment on every line and is the easiest template:
copy the section you need, delete the rest.

## 3. Check the file before you rely on it

```
compresscompare --check-config
compresscompare --check-config --config my-overrides.yaml
```

prints `<path>: OK (36 sites)` or an error that names the file, the key and the problem. (On macOS the
program is `CompressCompare.app/Contents/MacOS/CompressCompare`; on Linux `build/linux/compresscompare` or
wherever you installed it.)

```
configuration error: my-overrides.yaml: limits.max_download_bytes: value 0 is outside [1, 17179869184]
configuration error: my-overrides.yaml: heatmap.colourmap: unknown key (check the spelling against docs/reference/config.md)
configuration error: my-overrides.yaml: heatmap.colormap: 'rainbow' is not one of inferno, viridis, heat, ice, alert, gray
configuration error: my-overrides.yaml: schema_version: this build understands version 1
```

The same message is printed on start-up and the app refuses to run – a wrong configuration is never
silently ignored, and there is no fallback to defaults for a *partly* wrong file. An app started from Finder
has no console to print to, so on a Mac it simply closes again; run `--check-config` in Terminal to see why.

## 4. Things worth knowing

- **Unknown keys are errors.** A typo cannot silently do nothing.
- **Every key is range-checked** and a few rules span keys: `heatmap.gamma_min < gamma_max`, the SSIM window
  is odd, `threading.fallback_workers ≤ max_workers`, and so on. The error message says which rule failed.
- **Lists replace, they do not append** – except `sites`, which merges by name
  ([how to add a site](add-a-site.md)). To change `ffmpeg.search_subdirs` you list the whole list.
- **Units are in the key names**: `_px`, `_s`, `_ms`, `_bytes`, `_pct`, `_fraction`. `104857600` is
  100 MiB; the reference page shows the human-readable value next to each byte count.
- **The Settings window changes the same values for one session** (caps, OpenCL on/off, display options)
  without touching any file. Use it to experiment, then write the values you like into the override.
- The override file may be at most 4 MiB and nested at most 8 levels deep – limits you will not reach with
  hand-written files, but they are there so a malicious file cannot exhaust memory.
- Physical and standards constants (the sRGB curve, the CIE matrices, JPEG marker codes, the ΔE2000 weights)
  are **not** in the file on purpose: they are named constants in the source, because a wrong value there
  produces wrong numbers, not a different preference.

## Undoing it

Delete the override file (or rename it) and the built-in values are back. `compresscompare --check-config`
with no file found reports `built-in config.yaml: OK (36 sites)`.
