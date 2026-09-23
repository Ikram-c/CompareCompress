# How to load an image or video from a URL

Any slot accepts a direct `http://` or `https://` link instead of a file. The app downloads it, sniffs the
bytes to find out what it really is (sites routinely serve WebP under a `.jpg` name), decodes it, and – for a
site slot – picks the site from the link's host.

## Get a direct link

Sites embed images in pages; a page link returns HTML, not pixels. On the site, right-click the picture and
choose **Copy Image Address** (Safari, Chrome, Edge; *Copy Image Link* in Firefox) or **Open image in new tab** and copy that address. The
link usually points at a CDN host such as `pbs.twimg.com`, `scontent-….cdninstagram.com` or `i.redd.it`.

If you paste a page link anyway, the slot reports *that link returns a web page, not the image itself* and
tells you what to do.

## Paste it

Three ways, all equivalent:

1. Paste the link into the slot's text box and press **Enter** or **Fetch**.
2. Copy the link, hover the slot and press **Cmd+V** on macOS (**Ctrl+V** on Linux). Pressed anywhere else,
   it fills the next empty slot.
3. Right-click (on a Mac also two-finger click or Control-click) the slot's thumbnail area and choose **Paste URL / path from clipboard**.

The status line shows *downloading…* with a progress bar, then the format line of the decoded file.

## What happens to the site name

For a site slot with no site chosen yet, the host of the link is matched against the `hosts` lists of the
[site database](../reference/sites.md): `pbs.twimg.com` → X (Twitter), `i.pinimg.com` → Pinterest,
`cdn.discordapp.com` → Discord, and so on; the longest matching suffix wins. You can always overtype it.

## Limits and settings

- Downloads stop at `limits.max_download_bytes` (100 MiB) – or `limits.video.max_bytes` (250 MiB) if that is
  larger, because a video is downloaded to a temporary file for the video back-end. The *Settings* window changes both for
  the session; [config.yaml](change-config.md) changes them permanently.
- Redirects are followed (`network.max_redirects`, 10). Only `http` and `https` are accepted.
- `network.user_agent` and `network.accept` are the headers sent; some CDNs refuse requests without an
  `Accept: image/*`, which is why the default asks for images and videos explicitly.
- Transfers slower than `network.low_speed_limit_bytes_per_s` for `network.low_speed_time_s` seconds are
  abandoned.
- Downloads use libcurl: on macOS the copy in the system SDK, so nothing has to be installed; on Linux the
  distribution's libcurl found at build time. A build without libcurl says *URL fetching is not available in
  this build* in the text box.

Temporary files created for downloaded videos are deleted when the app exits.
