# Design principles: config.yaml, docstrings and the Power of 10

Version 0.2 rewrote the 0.1 code base around three rules: **no magic numbers** (every tunable value lives in
`config.yaml`), **docstrings instead of comments**, and **NASA/JPL's Power of 10** coding rules adapted to
a desktop C++17 application. Version 0.3, the macOS port that moved the image pipeline onto OpenCV and
OpenCL, keeps all three. This page explains each rule, how it is applied, and where and why the code
deviates. The how-to for running the checks is [Run the tests and the static analysis](../how-to/run-tests-and-static-analysis.md).

## One configuration file instead of magic numbers

A number that appears in code without a name is a magic number: nobody knows whether `0.005` is a
tolerance, a probability or a typo, and changing it means a rebuild. The rule adopted here splits every
literal into one of three classes:

| Class | Example | Where it lives |
|---|---|---|
| **Settings** – anything a user might want different: sizes, caps, time-outs, defaults, thread counts, UI geometry, the site database | `limits.max_download_bytes`, `metrics.ssim.window`, `heatmap.colormap`, `sites[]` | `config/config.yaml`, read into typed structs (`cc::Config`) at start-up |
| **Physical and standards constants** – values that are *right or wrong*, never a preference | the sRGB transfer curve, the D65 matrix, CIE ε and κ, JPEG marker codes, the IJG Annex K tables, the ΔE2000 weighting, 255 as the 8-bit maximum | named `constexpr` constants next to the code that uses them |
| **Cosmetic constants** – colours, paddings, corner radii, buffer sizes of the UI | `cc::style::kColClearColor`, `kNumberBufChars` | `src/app/ui_style.h`, one namespace, all named |

The first class is the interesting one. The file is the *single source of truth*: `cmake/EmbedFile.cmake`
compiles it into the executable as a byte array, so the program never depends on finding a file, and the
same file is what a user copies into the per-user settings folder (or next to the executable) to override
any subset of keys. Every key is
read through one `Reader` that knows the accepted range (`get_int("limits.max_site_slots", …, 1, 64)`),
so a wrong value is reported with its path, and a key the reader did not consume is an error – which is
what makes typos impossible to ignore. `tools/gen_config_reference.py` reads the same two files and writes
the [reference table](../reference/config.md), so the documentation cannot drift from the code either.

What deliberately did **not** move into the file: the constants of class two. Putting the sRGB curve in a
configuration file would only let a user make the colour maths wrong.

## Docstrings instead of comments

Every file, namespace, struct, member, function and constant carries a Doxygen docstring:

```cpp
/**
 * @brief Computes a heat map.
 * @param a Original.
 * @param b Site copy; must have the same dimensions as `a`.
 * @param m The metric.
 * @param cfg `metrics` section.
 * @return An invalid map when the inputs do not match.
 */
[[nodiscard]] HeatMap compute_heatmap(const Image& a, const Image& b, Metric m, const MetricsConfig& cfg, …);
```

The conventions: `@file` + `@brief` at the top of every file, stating what the file is *for* and any
design note that applies to the whole file (such as a Power of 10 exception); `@brief` on every
declaration; `@param` only for parameters the name does not explain; `@return` when the return value has
a convention (`-1`, an empty image, `false` with a message); `/**< … */` on struct members. Inside function
bodies, the rare note that is worth keeping is a `/* … */` block – there are no `//` comments left in the
sources apart from the conventional `} // namespace cc` closers and the GLSL code inside a string, where
the shader compiler sees them. The
[Doxygen run](../how-to/generate-api-docs.md) is configured to warn when a function documents some
parameters but not all, and it is clean.

A docstring says what a function guarantees; a comment tends to say what the author was thinking. The
first survives refactoring and can be rendered, searched and checked; the second rots.

## The Power of 10, adapted

Gerard Holzmann's ten rules (*The Power of 10: Rules for Developing Safety-Critical Code*, IEEE Computer,
2006) were written for embedded C with no dynamic memory and a fixed workload. A desktop application that
decodes user-supplied files of unknown size, talks to a GPU driver and drives an immediate-mode GUI cannot
follow all of them literally; the table says how each rule was applied and where an exception is made on
purpose.

| # | Rule | How it is applied here | Documented exceptions |
|---|---|---|---|
| 1 | Simple control flow: no `goto`, `setjmp`/`longjmp`, no recursion | No `goto`, and no `setjmp`/`longjmp` in this code: images are decoded through OpenCV's `cv::imdecode`, which keeps libjpeg's `setjmp`-based error handling inside OpenCV and reports a failure as an empty result or a `cv::Exception`. The fuzzy matcher is an **iterative** dynamic programme; the YAML merge and the unknown-key walk use explicit work lists; clang-tidy's `misc-no-recursion` is on. | none |
| 2 | Fixed upper bound on every loop | Every loop over user data is bounded by a configured limit (`limits.*`, `process.max_read_iterations`) or a named constant (`kMaxSegments`, `kMaxArgs`, `kMaxSites`, `kMaxMapEntries`, `kMaxDepth`, `kFuzzyMaxHaystackChars`). Files are read in bounded chunks with a byte cap. | The **GLFW event loop** and the **background worker's scheduler loop** run until the window closes / the worker quits; both are marked *Intentionally unbounded* in the source. |
| 3 | No dynamic memory after initialisation | Allocation is confined to well-defined moments: a file load, a comparison, a heat map – each one a job with a known upper size (`limits.max_image_edge_px`, the video caps, the texture ceiling). Per-frame code allocates nothing beyond what Dear ImGui does internally. | Images, heat maps and downloads **are** heap-allocated per job: their size is only known when the user picks a file. OpenCV allocates its `cv::UMat` buffers (in main or GPU memory) per operation in the same jobs. The rule's intent – no unbounded, unpredictable growth – is kept by capping every one of them. |
| 4 | Functions of at most 60 lines | Enforced: `tools/check_function_length.py` reports 0 functions over 60 lines, and clang-tidy's `readability-function-size` (60 lines, 120 statements, 8 parameters, nesting 5) runs in analysed builds. Long UI functions were split into `ui_*` helpers and a `ViewerFrame` struct; the longer functions added by the macOS port (the kernel self-test, the ImageIO decoder, the OpenCV video probe) were split the same way. | none |
| 5 | At least two assertions per function; assertions must recover | `CC_REQUIRE(condition, recovery)` checks a precondition and runs the caller's recovery (`return false`, `return {}`, …) when it fails; `CC_ENSURE(condition)` records a postcondition violation and continues. Both stay on in release builds, print file and line, and the count is reported at exit. There are ~60 of them, placed where they mean something (dimension and buffer invariants, configuration ranges, `nullptr` inputs) rather than two per function mechanically. | The *two per function* density is not reached: trivial getters and UI drawing functions carry none, on the reasoning that an assertion that cannot fail is noise, not safety. |
| 6 | Smallest possible scope for data | Everything is `const` unless it is written, declared at first use, and file-local (`namespace {`) unless another file needs it. The `App` state is private with friend access for the one frame helper. | The single global `g_app` pointer in `main.cpp` exists because GLFW's drop callback takes a plain function pointer. |
| 7 | Check every return value; check every parameter | Every function that can fail returns `bool` or an optional-like value and is marked `[[nodiscard]]`; the compiler flags an ignored result. Parameters are validated by `CC_REQUIRE` at the boundary (public `core` functions) and trusted inside. | `std::printf`/`fprintf` results on diagnostic output are not checked, and a handful of GL calls are drained collectively through `glGetError` after a batch (`drain_errors`). |
| 8 | Limited preprocessor use: includes and simple macros only | Macros are `CC_REQUIRE`, `CC_ENSURE`, the two feature flags `CC_HAVE_CURL` / `CC_HAVE_WINHTTP` (always defined as 0/1 so `-Wundef` works) and platform `#ifdef`s (`__APPLE__` for ImageIO, the `ui.ini` folder and the Retina scale). No token pasting, no macro-generated code – the OpenGL loader is 47 explicit declarations instead of an X-macro. | The OpenCL C source in `cl_kernels.h` names its standards constants with `#define`, because OpenCL C 1.2 has no `constexpr`; those macros exist only for the OpenCL compiler. |
| 9 | Restricted pointer use: one level of dereferencing, no function pointers | References and `std::vector`/`std::string` everywhere; raw pointers appear only for optional arguments (`Progress*`), C library boundaries and pixel rows. | **Function pointers and `std::function`** are used where a library demands a callback (GLFW, libcurl progress, Dear ImGui's `InputText` callback, `resolve<Fn>` for OpenGL entry points) and for the background jobs and their completions; each site has a *Power of 10 note* in its docstring explaining why. The CPU twins in `cv_ops.cpp` also hand their row loops to OpenCV's `cv::parallel_for_` as lambdas, the shape OpenCV's thread pool requires. |
| 10 | Compile with all warnings, pedantic, warnings as errors; use static analysers daily | `-Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wsign-conversion -Wcast-qual -Wformat=2 -Wundef -Wdouble-promotion -Wnull-dereference -Wnon-virtual-dtor -Wold-style-cast -Wimplicit-fallthrough -Werror` on Apple clang, clang and GCC, third-party headers (OpenCV's included) as system headers so the rule applies to *this* code only. clang-tidy (`.clang-tidy`, warnings as errors) and cppcheck (`cppcheck` target) both report nothing. | `CC_WARNINGS_AS_ERRORS=OFF` exists for exploratory builds; it is not the default anywhere. |

Two rules deserve a word more. Rule 3 is the one most often quoted against C++ desktop code, and the
answer here is not to pretend: the program allocates, but never without a ceiling that the user can see
in `config.yaml`, and never in a loop that could run away. Rule 9's ban on function pointers exists
because they defeat static analysis of the call graph; the exceptions above are all at the boundary to
libraries that define the callback shape, plus the job queue, where a `std::function` replaces what would
otherwise be a class hierarchy with one subclass per job – more code, same risk.

## Other habits that follow from the rules

- **Contracts over crashes.** Nothing in the program calls `abort()` or lets an exception escape. Exceptions
  are caught where a library throws them: the two public configuration entry points catch yaml-cpp's and
  turn them into messages with the file name; `cv::Exception` is caught at the OpenCV boundaries – around
  `cv::imdecode` and `cv::imencode` and the `cv::VideoCapture` calls – and becomes an error message in the
  slot; and every job swallows exceptions into a failed result.
- **A fallback for every accelerator.** Every OpenCL kernel has a CPU twin that computes the same values;
  when a kernel cannot run, the twin is used, and in `auto` mode OpenCL is only switched on after a start-up
  self-test has compared the two. A faulty GPU driver therefore costs speed, not correctness.
- **Detect by content, not by name.** File types are sniffed from bytes, because sites serve WebP as
  `.jpg`; the site is detected from the URL's host, not from what the user typed.
- **No shell.** External programs (`ffmpeg`, `ffprobe`, `zenity`, `kdialog`, `osascript`) are started
  with an argument vector, a time-out and an output cap, never through `system()`.
- **Everything that can run headless does.** The `core` library has no UI dependency, so the tests,
  `cc_bench`, the `--check-config` and `--system-info` switches and the smoke test (`--exit-after`) exercise
  the same code the GUI uses.
- **Generated, not maintained.** The configuration reference, the site table and the embedded
  configuration header are produced from the sources by scripts, so they cannot silently diverge.

## References

- G. J. Holzmann, *The Power of 10: Rules for Developing Safety-Critical Code*, IEEE Computer 39(6),
  2006.
- *JPL Institutional Coding Standard for the C Programming Language*, JPL DOCID D-60411, 2009 (the
  expanded rule set the Power of 10 summarises).
- D. Procida, *Diátaxis*, <https://diataxis.fr/> – the framework this documentation follows.
