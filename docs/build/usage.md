# Building, Running, and Deployment

BoredOS utilizes a highly modular architecture. Core userland binaries, shells, environments, and assets are isolated into specialized external repositories stored under `external/` and dynamically staged during compilation.

---

## Autonomic Dependency Fetching

External repositories are fetched and synchronized using the custom shell automation [tools/fetch_external.sh](file:///Users/chris/BoredOS/tools/fetch_external.sh). 

### How it works:
1. When compile commands like `make boredos.iso` or `make run` are executed, the Makefile triggers the `external-fetch` target.
2. The fetcher iterates over the repositories registered in `fetch_external.sh`.
3. **If a repository is not cloned**: It clones the repository using a fast, blobless clone (`--filter=blob:none`) and checks out the registered branch (usually `main`).
4. **If a repository already exists**: Rather than making slow `git pull` calls that can trigger API rate limits, the script performs a fast, non-blocking check:
   - It queries the remote Git server directly using `git ls-remote` 
   - If the local Git hash matches the remote branch head hash, it completely skips pulling.
   - If a new commit is detected on the remote, it pulls the updates.
5. **Parallel Safety Lock**: It secures a directory lock (`build/fetch.lock`) to prevent concurrent parallel execution when running high-performance parallel compiles (e.g. `make -j10`).

---

## The Staged Build & Staging Pipeline

The build process enforces a two-phase build pipeline:

```mermaid
graph TD
    A[make boredos.iso / make run] --> B[external-fetch]
    B --> C[build/sdk: Compile libc & setup build/sdk/]
    C --> D[Compile Kernel ELF]
    C --> E[Compile Modular Apps passing BOREDOS_SDK]
    D --> F[Assemble Initrd directory structure]
    E --> F
    F --> G[Dynamic Colloid Icon Scanning]
    G --> H[Create initrd.tar.lz4]
    H --> I[Limine bios-install & xorriso bootable ISO]
```

### The Compile Phases:
1. **SDK Bootstrap Phase**:
   - `external/libc` is compiled first. It installs standard headers and startup routines (`crt0.o`, `crt1.o`, `libc.a`) directly into a local target SDK folder: `build/sdk/`.
2. **Integrated Multi-Repo Compilation**:
   - The root Makefile builds all other external application repositories in parallel, explicitly passing `BOREDOS_SDK=$(abspath build/sdk)` to their sub-Makefiles.
   - The application sub-Makefiles detect this local SDK path, link immediately against it, and skip all local fetching or SDK rebuild routines, providing massive speedups.
3. **Dynamic Asset Resolution (The Colloid System)**:
   - The Colloid icon pack contains thousands of icons, which are too heavy to include in the compressed kernel RAM disk (`initrd`).
   - The root `Makefile` dynamically parses C/C++ source code files in all compiled external folders at build time to identify which `.png` files are actually referenced:
     ```make
     USERLAND_COLLOID_ICONS = $(shell find external -type f -name '*.c' ! -path '*/third_party/*' -exec grep -hoE '"[^"]+\.png"' {} + | sed 's/"//g')
     ```
   - Only the referenced icons are copied from the decoupled `external/colloid/` directory into `Library/images/icons/colloid/` in the initrd, keeping the RAM disk size tiny.
4. **Single-Pass Dispatch Target**:
   - Targets like `make run` and `make run-hd` resolve platform-specific emulation rules at parse-time using GNU Make conditionals (`ifeq ($(HOST_OS),Darwin) run: run-mac ...`), completely preventing duplicate sub-make execution of `fetch_external.sh`.

---

## 4. Standalone Repository Builds (Developer Friendly)

Every external repository features complete isolation and autonomy. A developer can copy or clone **any** of the directories inside `external/` and compile it independently outside the BoredOS environment:

- **Isolated Build Flow**:
  - If a sub-Makefile detects that `BOREDOS_SDK` is not defined, it knows it is being run standalone.
  - It automatically clones `https://github.com/boredos/libc.git` locally.
  - It builds it locally, caches the compiled artifacts at `build/sdk/`, and compiles the standalone application binary perfectly.

---

## 5. Minimum System Requirements

To run BoredOS successfully (either in emulation or on bare metal), your target machine should meet the following minimum requirements:

- **CPU**: An `x86_64` (64-bit) compatible processor.
- **Memory**: Approximately `~256 MB` of RAM.
- **Display**: A display, rendered via the framebuffer. (A GPU will not work.)
- **Networking (Optional)**: A compatible Network Interface Card (NIC) is required if you want to use the networking stack. (e.g. Intel E1000, Realtek 8139, or VirtIO NIC.)

---

## 6. Running and Deployment

### Emulation:
To test the built ISO image quickly in QEMU:
```sh
make run
```
To boot directly from an UEFI-enabled expandable hard disk image (after installing with boredos_install inside of BoredOS)
```sh
make run-hd
```

### Bare Metal Flashing:
1. Compile the project to generate `boredos.iso`.
2. Flash the ISO to a USB drive using a raw imaging tool (such as `dd` or **Balena Etcher**).
3. Disable **Secure Boot** in your target PC's UEFI firmware.
4. Select the USB drive from the bootloader menu. BoredOS boots natively on both legacy **BIOS** and modern **UEFI** systems.
