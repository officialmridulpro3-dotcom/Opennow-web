# The UI port

The web client's visual language is a dark-only "cyberpunk deck": pure black
ground, an electric blue/near-black layering system, one neon accent, chamfered
corners, and a strict four-step type scale. Everything below is a direct port of
`../src/client/styles.css` (20.8k lines) into two files, `ui/Theme.{h,cpp}` and
`ui/Widgets.{h,cpp}`, plus the view code in `app/views/`.

## Palette

`--color-void #000000` is the page. The deck is built by mixing
`--color-ultra #0000ee` into the void at fixed percentages, which is why the
greys are blue-tinted rather than neutral:

| Token | Value | Used for |
| --- | --- | --- |
| `void_` | `#000000` | page ground, behind everything |
| `bg_a` / `bg_b` / `bg_c` | ultra @ 9 / 16 / 7 % into void | surface steps |
| `panel` / `panel_border` | ultra @ 22 % / white @ 8 % | cards, sheets |
| `card` / `card_hover` / `card_selected` | ultra @ 14 % / 18 % / accent surface | posters, rows |
| `ink` / `ink_soft` / `ink_muted` | white @ 100 / 72 / 46 % | text steps |
| `accent` | neon `rgb(118,255,59)` by default | the one accent |
| `error` / `warning` / `info` / `success` | fixed hues | status tones |

The accent is a runtime choice, not a compile-time one: `apply_accent()` maps
`settings.appAccentColor` (green/blue/violet/amber/rose) onto an `AccentPreset`
and re-derives `accent_hover`, `accent_press`, `accent_glow`,
`accent_surface`, `accent_surface_strong` and `accent_on`, then pushes the whole
set into the ImGui style. `Palette` in `Theme.h` is the full field list.

## Geometry and chrome

- `--rail-w 68px`, `--navbar-h 48px`, `--header-h 72px`, `--statusbar-h 40px`.
- **Chamfered corners.** Every panel is a `clip-path: polygon(...)` in CSS; here
  it is `draw_panel(list, min, max, fill, border, chamfer, rounding, thickness)`
  emitting the same clipped geometry as ImGui draw commands. The chamfer is a
  parameter, so the rail, cards and modals all share the same silhouette.
- **One shadow.** `6px 5px 12px 3px rgba(29,28,28,0.21)` — `draw_shadow()`.
- **Glow.** `draw_glow()` for the accent's outer halo on focused/selected
  elements; `draw_gradient()` for hero backdrops.

`metrics()` returns the whole set as a struct so a layout change is a one-line
edit, and `content_scale` (from the platform) multiplies every dimension so the
UI is DPI-correct on Windows and macOS.

## Type

Dear ImGui has no runtime font scaling, so each of the design's four steps is
baked into the atlas as its own `ImFont`, and widgets pick one by pointer:

| Step | Size | Family |
| --- | --- | --- |
| `caption` | 11 px | body (Source Sans 3 / Roboto / Segoe UI) |
| `body` | 14 px | body |
| `title` | 18 px | body |
| `display` | 24 px | display (Montserrat Bold / Segoe UI Semibold / Helvetica) |
| `mono` | 14 px | mono (Cascadia / Consolas / Menlo / DejaVu Sans Mono) |

Fonts are loaded from the OS font directory per platform and fall back to
ImGui's built-in proportional font, so the binary carries no multi-megabyte
payload. `type_scale()` hands the five pointers to the widgets.

## Widget set

`ui/Widgets.h` is the component library, and it maps 1:1 onto the React
components. Layout primitives first:

```cpp
begin_surface(origin, size, fill, border, chamfer);   // a clipped panel
end_surface();
begin_scroll_child(id, size);                         // a scroll region
end_scroll_child();
shell_layout(origin, size);                           // rail + navbar + statusbar frame
wrapped_text(text, width, color);                     // returns the height consumed
```

Then the controls — each one carries the same states the CSS did (rest, hover,
active, disabled, focus ring):

```cpp
ButtonStyle style;                  // fill, border, text, chamfer, padding
button(label, style, size);
button_accent(label, size);         // the primary action
button_ghost(label, size);
button_danger(label, size);
icon_button(glyph, size, tooltip);
chip(label, selected);
toggle(label, &value);
slider_float(label, &value, min, max);
slider_int(label, &value, min, max);
combo("##id", labels, &index);
input_text(label, &value);
input_key(label, &value);           // shortcut capture field
```

Composites:

```cpp
section_header(title, subtitle);
setting_row(label, hint);   setting_row_end();
PosterCard card; poster_card(card, selected);   // 2:3 art + title + meta
card_row(id, cards, height, on_select, on_activate, &selected);
hero(title, subtitle, backdrop, primary, secondary, on_primary, on_secondary);
hud_line(label, value);  hud_panel(title, body_fn);
progress(fraction, label);  spinner(label, radius);
```

Feedback:

```cpp
toast(ToastKind, title, detail);  draw_toasts();
modal(...);                       // centred sheet with a title, body and buttons
```

ImGui gotchas that shaped the API (all of them cost a compile cycle to learn):
`PushFont` takes only `ImFont*`; textures are `ImTextureID`, not `ImTextureRef`;
`ImVec2` has no arithmetic operators; `InputText` has no `std::string` overload;
there is no `ImLerp`; and several colour/flag enums the CSS implied simply do
not exist (`ImGuiCol_InputTextCursor`, `ImGuiCol_DockingPreview`,
`ImGuiWindowFlags_NoDocking`, …), so the equivalents are hand-rolled.

## Views

`app/views/` holds one file per screen. Each view is a free function taking
`(App&, AppState&, origin, size)` and drawing with the widgets above — the
direct analogue of a React component, minus the reconciler.

| File | Screen |
| --- | --- |
| `ShellViews.cpp` | the shell: rail, navbar, header, statusbar, the router itself |
| `LoginView.cpp` | device-login code entry, account restore, provider pick |
| `HomeView.cpp` | store hero, shelf rows, search, continue-playing |
| `DetailsView.cpp` | game details: art, variants, store links, ownership, play CTA |
| `PlaytimeView.cpp` | the ledger: stat tiles, ranking, cadence, recent sessions |
| `SettingsView.cpp` | all nine sections, every setting row |
| `StreamView.cpp` | the stream surface, HUD, sidebar, stop/stat controls |
| `Toasts.cpp` | the toast queue and modal host |

Keyboard navigation works because the widgets are ImGui widgets: `Tab` moves
focus, `Enter` activates, arrows move within lists, and the shortcut bindings in
Settings are captured through `input_key()` and stored in `Settings`.

## i18n

All twelve locales are compiled in. `tools/gen_i18n.py` flattens
`../locales/en.json` (the base) plus each locale's overrides into
`src/i18n/TranslationData.cpp` and `include/onow/i18n/TranslationData.h` — about
7,900 entries. `I18n` does:

- `{{name}}` interpolation,
- `_plural` sibling selection for `t_count`,
- fallback to English for anything a locale does not translate (418 keys
  currently fall back, which is expected — the overrides are partial).

Regenerate after editing a locale file:

```sh
make i18n
```

And audit that no `tr("...")` call names a key that does not exist — a typo
would otherwise render the key itself, because `I18n::lookup` echoes unknown
keys:

```sh
make check-keys     # 210 keys referenced, 0 missing
```
