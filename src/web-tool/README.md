# bbs-fw web config tool

A browser-based replacement for the WPF config tool (`src/tool`), using the
[Web Serial API](https://developer.mozilla.org/en-US/docs/Web/API/Web_Serial_API)
to talk to the controller instead of a desktop serial port library. See
[ROADMAP.md](../../ROADMAP.md) (Phase 1.5) for why and how this is scoped.

Only Chromium-based browsers (Chrome, Edge, Opera, Brave) support Web
Serial — Firefox and Safari don't.

This only understands this fork's current config format (`CONFIG_VERSION`
6+). A stock/upstream bbs-fw install needs the original WPF tool.

## Develop

```
npm install
npm run dev       # local dev server with hot reload
npm test          # protocol layer unit tests
npm run build     # type-check + production build
```

## Layout

- `src/protocol/` — the wire protocol: checksums, the `Configuration` model
  (parse/serialize/validate), and the `BbsfwConnection` class wrapping Web
  Serial. Has no DOM dependency, so it's unit-testable without a browser or
  real hardware (see `*.test.ts`).
- `src/ui/` — small DOM-binding helpers, no framework.
- `src/main.ts` — wires the protocol layer to the form in `index.html`.
