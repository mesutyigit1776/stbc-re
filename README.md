# stbc-re

This is what's left of my two attempts to decompile Star Trek: Bridge Commander (the 2002 game, `stbc.exe`, GOG version 1.1). Neither attempt got anywhere close to finished, but a fair amount of what I collected on the way is still usable, so I'm putting it here instead of letting it rot on my disk.

No game files in here, obviously. No mods and no SDK either. If you want to run the emulator tests you need the real exe from your own copy (GOG still sells the game).

Fan project, nothing to do with Activision or Paramount.

## Where it ended up

Not far, honestly. The exe has something like 23,500 functions and I have C++ for 461 of them, so about 2%. 356 of those are tested against the original machine code in an emulator; the other 105 I only compared by reading the disassembly, so be more careful with those. Nothing is byte-matched and nothing here opens a window. It's a pile of small checked pieces, not a game.

If you just want to play BC on a modern machine, [bc_dauntless](https://github.com/markward/bc_dauntless) is where you should be looking. It's way ahead of anything in this repo, and finding it is partly why I stopped.

## What's in here

The tables are probably more useful to you than the code.

| File | What it is |
|---|---|
| `data/names/verified.tsv` | 4,837 function names, each one tied to an evidence id |
| `data/names/candidates.tsv` | 11,493 names from the first attempt. Unchecked, see below |
| `data/names/conflicts.tsv` | the 170 addresses where those two tables disagree |
| `data/evidence.tsv` | 250 claims about the exe with the tool output behind each |
| `data/bugs.tsv` | 41 bugs in the original exe (kept on purpose in the reconstruction) |
| `data/layouts/` | field offsets and vtable slots for 58 classes |
| `data/python_changes.tsv` | where the exe's Python is not the same as stock 1.5.2 |
| `target/`, `interface/` | the script API: 3,990 wrapper functions with their argument formats |
| `data/first-attempt/` | 23 smaller tables from the first attempt: vtables with their slot addresses, class sizes and constructors, a class tree, event type ids, script constants and argument formats, and notes on how the exe's Python matches CPython 1.5.2. Unchecked too, and they have no header rows |

On the code side, `src/engine` is the reconstructed C++. `src/emutest` and `tests/emu` are the emulator harness: it loads one function from the original exe into Unicorn, runs it, runs my version with the same inputs and compares what comes out. `src/pyhost` gets the game's Python 1.5.2 building on a 64-bit machine and generates the `App` wrappers, which is enough to import the game's scripts but not enough to run a mission.

There is also `legacy/first-attempt/`. That's the C engine from the first attempt (about 18,000 lines), which went a different way: instead of copying the exe function by function it tried to be a new engine that runs the game's scripts. Back then it could run a Quick Battle from the game's scripts, but I haven't re-run it since. It doesn't build from this repo, because the generated wrappers and the game scripts it needs are not here, and none of it was tested against the exe. I left it in because the way the parts connect might be worth a look for somebody.

## Read this before you use the names

There are two name tables and they are not the same quality at all.

Every name in `verified.tsv` points to a row in `evidence.tsv`, and that row says what was seen in the exe, where and what result would prove it wrong. Sounds great, but 3,990 of the 4,837 are script API wrappers that can be read almost directly from the method tables in the exe. That part was easy. Actual engine function names are only a few hundred.

`candidates.tsv` comes from the first attempt and I'd be careful with it. Those names were produced by language models reading Ghidra output and voting on a name (three runs per function), and nobody checked them against the exe. When I audited one batch later, roughly 12% were wrong. They're fine as a hint for where to look. Just don't build on them without checking first.

In that file, `applied` means the name won the vote and I used it back then, `high` means all three runs agreed, and `medium` means they didn't quite. Also, if you see the same address twice in `verified.tsv` it's not a mistake; a few API names share one function.

## Building

CMake 3.20+, a C++17 compiler and Python 3.

```bash
cmake -B build -S .
cmake --build build -j
```

I only ever built this on an Apple Silicon Mac. Linux and Windows might work, I never tried.

For the emulator tests, put your exe at `game/stbc.exe` and check the hash before anything else. Every address in this repo assumes exactly that file, so with a different exe you'll get garbage.

```bash
shasum -a 256 -c checksums/stbc.exe.sha256
python3 -m venv tools/.venv
tools/.venv/bin/pip install -r tools/requirements.txt
tools/.venv/bin/python -I tests/emu/run.py
```

## Who did the work

Mostly AI, with me steering. Ghidra 12.1.4 did the disassembly. In the first attempt cheaper models did the naming (that's where the unchecked names come from), and I wrote a bit of C by hand, but it's nothing at this scale. The second attempt was Claude (Opus 5.5 and Sonnet 5.5) writing the code and testing its own claims in the emulator. Claude Mythos 5.2 went through both attempts afterwards and put this repo together.

I'm saying this openly so you know what to trust. The emulator-tested functions I'd rely on. Everything else, only as far as its evidence row goes.

## License

Code is GPL-3.0-or-later (`LICENSE`). The tables in `data/`, `target/` and `checksums/` are CC0 (`LICENSE-DATA`). `third_party/Python-1.5.2/` keeps its own terms, see `Misc/COPYRIGHT` in that folder.
