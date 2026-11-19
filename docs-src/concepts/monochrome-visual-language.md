---
title: Monochrome visual language
summary: Typography and line-art rules for bringing Jupiter's designer fonts and shapes to a green-phosphor MDA display.
order: 30
icon: monitor
audience: developers
status: draft
---

Monochrome inherits Jupiter's typography and graphic vocabulary. The MDA monitor supplies the color: green phosphor on a dark screen. There is no application palette to design, tune, or reproduce. The firmware's pixel values express **intensity**, not different colors.

## Inherit from Jupiter

Use the type and shape language already defined by the [Jupiter design system](https://github.com/belikh/design):

| Jupiter role | Display use | Rendering rule |
| --- | --- | --- |
| SGI Alt | Screen titles and short, prominent words | Use a purpose-made pixel glyph set or atlas; preserve the typeface's distinctive forms rather than stretching a web font at runtime. |
| SGI Text | Instructions and readable supporting text | Keep text short, left aligned, and legible at the chosen raster size. |
| JetBrains Mono | IP addresses, ports, counters, timings and protocol values | Use fixed-width glyphs so columns and changing values stay aligned. |
| Jupiter line graphics | Rules, icons and illustrations | Start from the shared vector artwork; rasterize to the target grid and preserve its silhouette and proportions. |

The SGI fonts are designer typefaces, not a requirement to draw arbitrary TrueType outlines in the firmware. Font rasterization and atlas generation belong in the Jupiter build pipeline. Firmware should consume the generated, display-ready glyph data; it should not carry a second hand-designed font or copies of the font source.

Likewise, source line art from Jupiter's graphics package rather than inventing a parallel icon style. A shape intended for the MDA should have a monochrome raster output generated from that shared artwork, not a locally redrawn substitute.

## The display's visual grammar

The signal is 720 × 350 pixels at about 50 Hz. The framebuffer's existing values are:

| Pixel value | Meaning | Use |
| --- | --- | --- |
| `0x00` | Phosphor off | Background and clear space |
| `0x01` | Normal phosphor intensity | Body text, fine outlines, dividers and secondary marks |
| `0x03` | Bright phosphor intensity | Heading emphasis, key values and the most important parts of a shape |

Use brightness sparingly to establish hierarchy. Do not assign semantic color meanings to intensity: success, warning and error must be communicated with words or distinct shapes, not a color mapping. The display has no color-management or light/dark theme switch.

Jupiter's visual character is quiet, precise and line-led. On this display, favor open space, thin outlines, simple geometric frames and a small number of strong typographic anchors. Prefer an outline or continuous stroke over a solid block. Use filled areas only where the image needs a clear signal or a deliberate large-scale contrast.

## Raster and layout rules

- Design against the real 720 × 350 framebuffer, not a stretched 4:3 mockup. The CRT's pixels are not square; firmware geometry accounts for this with a horizontal aspect correction of about 1.54.
- Keep important content inside a deliberate inset. The calibration pattern's 72-pixel columns and 35-line rows are test geometry, not a mandatory application grid.
- Snap horizontal and vertical strokes to whole pixels. Avoid subpixel line widths and fine detail that disappears when reduced to the target raster.
- Preserve the source proportions of a Jupiter glyph or graphic. Scale deliberately, then inspect the resulting pixels; never squash artwork to fit a box.
- Keep neighboring strokes distinct. Dense details, tight counters and closely spaced parallel lines can merge on a phosphor display.
- Use fixed-width type for changing numeric values and reserve the brightest intensity for a small number of focal elements.
- Treat animation as information. Keep its silhouette readable at roughly 50 frames per second and avoid motion that adds no meaning.

## Shared source and generated outputs

The source of truth stays in `belikh/design`: SGI Alt, SGI Text and JetBrains Mono for typography; Jupiter graphics for line art. Display-specific glyph bitmaps and shape rasters are **generated derivatives**, not new sources of design. Keep generated build artifacts out of this repository unless the Jupiter publishing workflow explicitly requires a checked-in fixture.

The current firmware has a small 5×7 digit font for its network status screen and can receive 1-bit or 2-bit frames over the network. Those are implementation capabilities, not the finished Jupiter type system. The next integration step is to add a Jupiter-generated bitmap-font and monochrome-shape target, then consume its output here. Until that target exists, do not claim that the existing firmware renders the SGI fonts or Jupiter graphics directly.

## Review checklist

1. Is the artwork recognizably from Jupiter's type and graphic vocabulary?
2. Does it remain clear using only phosphor off, normal intensity and bright intensity?
3. Are meaning and status understandable without color?
4. Do letters retain their intended shape and readable spacing at the actual raster size?
5. Do thin strokes survive on the real monitor without crowding or filling in?
6. Is the aspect-corrected result checked on hardware, not only in a square-pixel preview?
