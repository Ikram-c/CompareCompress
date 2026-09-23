Synthetic test set (generated, not real uploads); used by docs/tutorials, test_video and the smoke test.
  original.png   1600x1200 source image
  instagram.jpg  1080 px wide, JPEG q75 4:2:0      -> "Instagram-like"
  facebook.jpg   centre-cropped to 1:1, JPEG q70   -> exercises the aspect-ratio/crop path
  discord.webp   1200 px WebP q80                  -> decoded by OpenCV's bundled libwebp
  original.mp4   640x360 H.264, 4 s (testsrc2)
  tiktok.mp4     480x270 H.264 at 150 kb/s         -> "TikTok-like"
Try (macOS):  open -a "$PWD/dist/CompressCompare.app" --args "$PWD/samples/original.png" "$PWD/samples/instagram.jpg" "$PWD/samples/facebook.jpg"
Try (Linux):  build/linux/compresscompare samples/original.png samples/instagram.jpg samples/facebook.jpg
Tutorial: docs/tutorials/first-comparison.md
