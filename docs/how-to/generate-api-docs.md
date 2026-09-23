# How to generate the API documentation

Every file, function, class, member and constant in `src/` carries a Doxygen docstring (`/** @brief … */`
with `@param` / `@return` where they add something). Doxygen turns them into a browsable HTML reference
with include graphs and a source browser. The docs in this folder (tutorials, how-tos, reference,
explanation) are included in the same site, so one output covers everything.

## Install Doxygen

- macOS 12: `sudo port install doxygen graphviz` (MacPorts), or the *Doxygen.app* disk image from
  <https://www.doxygen.nl/download.html>, whose command-line program is
  `/Applications/Doxygen.app/Contents/Resources/doxygen`.
- Debian/Ubuntu: `sudo apt install doxygen graphviz`. Fedora: `dnf install doxygen graphviz`.

Graphviz (`dot`) is optional; without it the include and class diagrams are skipped and everything else is
generated.

## Generate

Either through CMake (the `docs` target exists whenever Doxygen was found at configure time):

```
cmake --build --preset macos-intel --target docs       # or --preset linux-system
```

or directly, from the project root:

```
doxygen Doxyfile
```

Both write `build/doxygen/html/`. Open `build/doxygen/html/index.html` in a browser. The run takes a few
seconds and prints nothing when all docstrings are complete; a warning names the file, line and the
parameter that lacks a description.

## What is where

- **Files** – one page per source file with its `@file` description, the functions it defines and its
  include graph. Start at *Files → src/core* for the library, *src/app* for the UI.
- **Namespaces** – `cc` (the library and app), `cc::gl` (the OpenGL loader), `cc::style` (UI constants),
  `cc::cvops` (the OpenCL kernels' launchers and their CPU twins), `cc::generated` (the embedded
  configuration).
- **Classes** – every struct, including the configuration structs whose members mirror `config.yaml` key by
  key (`cc::Config`, `cc::LimitsConfig`, `cc::HeatMapConfig`, …).
- **Related pages** – the pages of this documentation folder.

`Doxyfile` documents the macOS build with libcurl (`__APPLE__` and `CC_HAVE_CURL=1` are predefined), so the
ImageIO decoder and the libcurl download path appear. The source also keeps `#ifdef _WIN32` branches
(Win32 process and dialog code, a WinHTTP download path); Windows is not a target of this port and those
branches are compiled out of the generated reference.

## Keeping it complete

The docstring rules the code follows are in
[Design principles](../explanation/design-principles.md) (section *Docstrings instead of comments*): a `@brief` on
every declaration, `@param` for parameters whose meaning the name does not give away, `@return` when a
return value needs explaining, `/**< … */` on struct members, and `/* … */` (never `//`) for the rare note
inside a function body. `WARN_NO_PARAMDOC = YES` in the `Doxyfile` reports a function that documents some
of its parameters but not all, so a partial list cannot slip in.
