# Images

Pack icons and banner images, loaded from Modrinth's CDN (`cdn.modrinth.com`). Where they appear is described in [discover.md](discover.md), [library.md](library.md), and [downloads.md](downloads.md).

## `ImageCache` (`src/UI/ImageCache.{h,cpp}`)
- `main()` owns one instance, created after the GL context and sharing the app's `HttpClient`.
- `Get(url)` returns a `LoadedImage` (texture, width, height, `readyTime`) once the image is ready, or `nullptr` until then.
  - The first call for a URL queues it.
  - A URL that failed to load or decode is never retried, so its placeholder stays.
  - Empty URLs always return `nullptr`.
- Four worker threads take URLs off the queue. Each one downloads the image with `HttpClient::Get` and decodes it to RGBA:
  - WebP (detected by its `RIFF....WEBP` header) via libwebp's `WebPDecodeRGBA`. Animated WebP isn't supported and counts as a failure.
  - PNG, JPEG, or GIF (first frame) via `stb_image`.
  - Images larger than 2048px on either side are downscaled with `stb_image_resize2` (sRGB-aware). Raw gallery uploads can be 4K or larger.
- `Update(now)` runs on the UI thread at the start of each frame. It uploads newly decoded images as OpenGL textures, with mipmaps so icons stay smooth when drawn small.
- `Shutdown()` stops the workers, cancelling in-flight downloads through the same cancel flag as other requests, and deletes all textures. `main()` calls it before tearing down the GL context.
- Images are cached in memory for the life of the app. There is no disk cache, so icons are downloaded again after a restart.

## Image sources

| Image | URL |
|---|---|
| Pack icon | `Modpack::iconUrl` (search result `icon_url`, usually 96px) |
| Banner thumbnail | `Modpack::bannerUrl` (search result `featured_gallery` or first `gallery` entry, 350px) |
| Full-size banner | `Modpack::bannerHiResUrl` (`raw_url` from `FetchModrinthBanner`, see [modrinth-search.md](modrinth-search.md)) |

Local packs from `resources/index.json` have no images and always show the placeholder.
