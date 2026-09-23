# How to add or edit a site in the site database

The list of sites – the names in the *Specify site* dropdown, their search aliases, the CDN hosts used to
auto-detect a site from a pasted URL, and the notes shown on hover – is the `sites` list in `config.yaml`.
You extend or correct it with an override file; nothing has to be rebuilt.

If you have not written an override before, read [How to change a setting](change-config.md) first: it
explains where the file goes and how to check it.

## A site entry

```yaml
sites:
  - name: Lemmy / Pixelfed          # shown in the dropdown and the slot header; required
    aliases: [lemmy, pixelfed, fediverse]   # what the fuzzy search also matches; optional
    hosts: [lemmy.world, pixelfed.social]   # host suffixes that auto-detect this site; optional
    notes: >-                        # tooltip text; optional
      Pixelfed re-encodes uploads as JPEG (quality is an admin setting, typically 80-90).
```

- `name` is required, unique (case-insensitively) and at most `limits.max_site_name_chars` (128) characters.
- `aliases` and `hosts` are lists of up to 64 strings each; they are lower-cased when loaded.
- A host matches a URL when it is equal to the URL's host or is a suffix of it after a dot: `fbcdn.net`
  matches `scontent-ams4-1.xx.fbcdn.net`. When several sites match, the longest suffix wins, so a specific
  `photos.example.com` beats a generic `example.com` on another site.
- `notes` is free text; it is wrapped in the tooltip. Keep it factual – the numbers the app measures are
  the truth, the notes only say what to expect.

## Add a new site

Write an override file with just the new entry:

```yaml
sites:
  - name: Nextcloud
    aliases: [nc, nextcloud, owncloud]
    hosts: [nextcloud.example.org]
    notes: Stores the original; the preview endpoint serves resized JPEGs.
```

Any name that does not exist yet is **appended** to the built-in list. The dropdown ranks by match quality,
so the position in the list rarely matters.

## Edit a built-in site

Use the same name as the built-in entry (case does not matter) and list only the fields you want to replace:

```yaml
sites:
  - name: Instagram
    hosts: [instagram.com, cdninstagram.com, instagram.fbcdn.net, ig.example-mirror.net]
```

Fields you list replace the built-in ones wholesale (the whole `hosts` list, not just one host); fields you
leave out are kept. To rename a site, add a new entry and leave the old one – there is no way to delete a
built-in site, but a site whose aliases and hosts you empty (`aliases: []`, `hosts: []`) will never be
auto-detected and only appears when you type its name.

## Check it

```
compresscompare --check-config
```

reports `…: OK (37 sites)` – the number tells you whether the entry was appended (37) or merged (36).
Mistakes are reported with the entry's position, for example
`my-overrides.yaml: sites[0].name: required, must be a non-empty string` (a problem found while merging, so
the index is the one in *your* file) or
`my-overrides.yaml: sites[36].hots: unknown key (a site has name, aliases, hosts and notes)` (found after
merging, so the index counts the 36 built-in entries first – an appended site is `sites[36]`, an edited
Instagram entry is `sites[0]`).

Then start the app, type the alias into a *Specify site* box and paste a URL from one of the hosts into a
slot: the header should switch to the new site's name.

## Where the names show up

- The `sites` **name** is the label in the slot header, the site selector above the canvas, the default
  file name of an exported heat map (`heatmap_nextcloud.png`), and the Survey tiles.
- The **aliases** feed the fuzzy matcher (`fuzzy` section of `config.yaml`: exact prefix bonus, gap
  penalties, minimum score) and the exact lookup used when you press Enter.
- The **hosts** are used only for URL auto-detection, and only when the slot has no site yet.
- The last built-in entry, *Other / custom*, is the catch-all for anything you cannot name.

The complete built-in list is in the [site database reference](../reference/sites.md).
