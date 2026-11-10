# Distributing an App (`trusscli dist`)

`trusscli build` makes an executable that runs on your machine. `trusscli dist` packages it so it runs on another one: a folder with everything the app needs, and optionally an archive or an installer.

```bash
cd myApp
trusscli dist                 # build (RelWithDebInfo) and write dist/<name>-<version>-<platform>-<arch>/
trusscli dist --archive       # also write a zip (Windows, macOS) or a tar.gz (Linux)
trusscli dist --installer     # also write an installer: setup.exe (Windows), a pacman package or .deb (Linux)
trusscli dist --dry-run       # list what would go into the package, change nothing
trusscli dist --init          # write a dist.json template and stop
```

Other options:

| Option | What it does |
|---|---|
| `--release` | Build as Release, without debug symbols |
| `--no-build` | Package the existing build (a Debug build is refused) |
| `-o <dir>` | Output folder (default: `dist/` in the project) |
| `--app-name`, `--app-version`, `--app-author` | Override the values in `dist.json` |
| `-j`, `--verbose`, `-p` | As in `trusscli build` |

## 1. What goes into the package

The top of the package is your project's `bin/` folder, minus build leftovers:

| In the project | In the package |
|---|---|
| `bin/myApp` (`myApp.exe`, `myApp.app`) | `myApp` |
| `bin/data/...` | `data/...` (macOS: inside the `.app`, `Contents/Resources/data/`) |
| `bin/otherApps/...` | `otherApps/...` |
| `dist.json` `include` entries | where their `to` says |
| `EULA.txt` in the project folder, or `eulaFile` | `EULA.txt` |
| (written by dist) | `licenses/`, `THIRD_PARTY_NOTICES.txt` |
| (written by dist, Linux) | `myApp.sh`, the launcher |
| (written by dist, Windows) | the VC++ runtime DLLs the app imports |

Windows `.pdb` files go to a separate `<name>-<version>-windows-<arch>-symbols/` folder next to the package. Keep it to read crash reports; do not ship it.

### Other programs and large files

To ship a helper program, a script with its runtime, or large data with the app, put it in a folder under `bin/`:

```
myApp/bin/otherApps/yourApp/yourApp.sh   ->   <package>/otherApps/yourApp/yourApp.sh
```

The project template's `.gitignore` keeps only `bin/data/` in git. For another folder, add it:

```
bin/*
!bin/data/
!bin/otherApps/
```

Find the files from the app relative to the app itself, never by the path on your machine: `getDataPath("tools/x")` for files under `data/`, or `getExecutableDir()` for files next to the executable. The same code then works during development and after installation.

Files that live outside the project (shared by several projects, or too large for git) go in with `include`. They are copied:

```json
"include": [{ "from": "../shared-tools", "to": "data/tools" }]
```

`from` is read on your machine when packaging; a path relative to the project folder works on every developer's machine and in CI, an absolute one only on yours. `to` is a path inside the package.

### Symlinks

A symlink in `bin/` or in an include folder is packaged as the same link when it is relative and points inside that folder, such as `libfoo.so.1 -> libfoo.so.1.2` (on Windows, which needs a privilege to create links, as a copy under the link's name). Any other link stops `dist` with an error that shows the `include` entry to use instead, because the package would not hold what it points to.

### Names

The executable keeps the name the build gave it, so `dist` takes it from the build. The app name (`name` in `dist.json`, the project folder name by default) is what people see: the package folder, the Start menu, the Windows installer, the Apps & features list. It may contain spaces and non-ASCII characters.

Linux package names only allow `a-z`, `0-9` and `+ . _ -`, so other characters are dropped there (`My App 한글` becomes `my-app`). Set an ASCII `name` in `dist.json` if that matters, for example to keep two apps with non-ASCII names apart.

## 2. `dist.json`

Optional. `trusscli dist --init` writes it with every key, so turning something on is changing a value:

```json
{
  "name": "myApp",
  "version": "0.1.0",
  "author": "",
  "eulaFile": "",
  "include": [],
  "exclude": [],
  "licenses": [],
  "installer": {
    "showEula": false,
    "showNotices": false
  }
}
```

| Key | Default | Meaning |
|---|---|---|
| `name` | the project folder name | The app name people see |
| `version` | `0.1.0` | Three numbers, `1.2.0` (a leading `v` is dropped). The Windows installer adds a fourth, `0` |
| `author` | `""` | Person, studio or company: publisher in Windows, `© <year> <author>` in the notices |
| `eulaFile` | `""` | Your license terms, shipped as `EULA.txt`. Empty: `EULA.txt` in the project folder if there is one. A file named here must exist |
| `include` | `[]` | `[{"from": "...", "to": "..."}]`: files or folders from outside `bin/` |
| `exclude` | `[]` | Glob patterns left out of the package: `"data/**/*.psd"`, `"data/raw"` |
| `licenses` | `[]` | `[{"name": "Node.js", "file": "..."}]`: your own third-party notices |
| `installer.showEula` | `false` | Windows installer: an EULA page with "I Agree" |
| `installer.showNotices` | `false` | Windows installer: a read-only page with the third-party notices |

Unknown keys give a warning and are ignored.

## 3. Windows

`--installer` builds `<name>-<version>-windows-<arch>-setup.exe` with NSIS (free, zlib/libpng license). Install NSIS once:

```
winget install NSIS.NSIS
```

The installer installs for all users under Program Files and registers in Apps & features, with `author` as the publisher. It asks the person installing whether to add shortcuts (checked by default) and to start the app at login (unchecked). With `showEula`, the person has to click "I Agree" to install; with `showNotices`, the third-party notices are shown for reading. The uninstaller removes exactly the installed files.

Without NSIS, `--installer` fails after the folder (and archive) are written.

The package holds the VC++ runtime DLLs the app imports, so the app runs on a PC without the Visual C++ Redistributable. A Debug build is refused (its runtime cannot be redistributed). An app that uses Media Foundation (video) gets a warning: Windows N / KN editions do not have it.

## 4. Linux

### Why a Linux build runs where it was built

The executable uses the distribution's shared libraries (ffmpeg, GTK 3, GStreamer, X11, GL, ...) down to their version numbers, and needs at least the glibc it was built with. Built on current Arch, an app needs `libavcodec.so.63` and glibc 2.43; Ubuntu 24.04 has `libavcodec.so.60` and glibc 2.39, so it does not start there. `dist` does not bundle these libraries: glibc, GL drivers and X11/Wayland break when copied to another system, and a distribution's ffmpeg may be built with GPL codecs.

So `<platform>` in the package name is the distribution and release it was built on, from `/etc/os-release`: `arch`, `endeavouros`, `ubuntu24.04`, `linuxmint22`. Derivatives that use their base's repositories usually run each other's builds: an `endeavouros` build runs on Arch, a `linuxmint22` build on Ubuntu 24.04.

### Packages (`--installer`)

`--installer` builds a package for the distribution `dist` runs on: a pacman package on Arch and its derivatives, a `.deb` on Debian, Ubuntu and their derivatives. On other distributions use `--archive`.

The package installs the app to `/opt/<package>/`, links `/usr/bin/<package>` to the launcher, and adds a Start menu entry with its icon (the first PNG in the project's `icon/` folder, else TrussC's). Its dependencies are found from the libraries the app loads, so the package manager installs them with it. People install and remove it with:

| | Install | Remove |
|---|---|---|
| pacman (Arch, EndeavourOS, Manjaro, ...) | `sudo pacman -U <package>-<version>-1-x86_64.pkg.tar.zst` | `sudo pacman -R <package>` |
| .deb (Debian, Ubuntu, Mint, ...) | `sudo apt install ./<package>_<version>_amd64.deb` | `sudo apt remove <package>` |

(The `./` matters: without it apt looks for the name in its repositories.)

Packages do not ask questions during installation, so there is no desktop shortcut and no "start at login" choice.

Tools needed to build them: `makepkg` and `objdump` (`sudo pacman -S --needed base-devel`), or `dpkg-deb` and `dpkg-shlibdeps` (`sudo apt install dpkg-dev binutils`).

### The folder and the tar.gz

The folder runs on the same distribution and release with the same libraries installed: start `myApp.sh`, which also finds `.so` files you put next to the executable. For other people, prefer a package. To run the folder elsewhere, the person needs the libraries the app uses; `ldd myApp | grep "not found"` lists the missing ones.

### Packages for another distribution

Build in a container of that distribution. With [distrobox](https://distrobox.it/), on any distribution with docker or podman:

```bash
# once: an Ubuntu 24.04 container with TrussC's build dependencies
distrobox create --name ubuntu2404 --image docker.io/library/ubuntu:24.04
distrobox enter ubuntu2404 -- sh -c 'sudo apt-get update && sudo apt-get install -y build-essential cmake git'
```

distrobox shares your home folder, so a TrussC checkout there is visible in the container. Build outputs would mix, though (`tools/build`, `core/build-linux`, your project's `build-linux` and `bin/`): use a separate checkout or git worktree of TrussC, and a copy of the project, for the container.

```bash
git -C ~/TrussC worktree add ~/TrussC-ubuntu        # a second checkout for the container
distrobox enter ubuntu2404
  cd ~/TrussC-ubuntu/tools && ./install_dependencies_linux.sh -y
  mkdir -p build && cd build && cmake .. && cmake --build . -j
  cd ~/myApp-ubuntu && ~/TrussC-ubuntu/tools/bin/trusscli dist --installer
```

The result is named `ubuntu24.04` and its `.deb` carries Ubuntu's dependencies. To try it as someone without your build tools would, install it in a clean container: `docker run --rm -it -v "$PWD/dist:/pkg" ubuntu:24.04`, then `apt update && apt install /pkg/<package>.deb`.

## 5. macOS

`dist` copies the `.app` with `bin/data` in `Contents/Resources/data`, writes the version and copyright into `Info.plist`, and signs the bundle ad hoc. An app signed ad hoc is blocked by Gatekeeper when downloaded from the internet (open it with right-click > Open); copying it by USB or a network share is fine. Developer ID signing, notarization and a `.pkg` installer are not done yet.

## 6. Licenses and the EULA

`dist` puts TrussC's `LICENSE.md` and each addon's `LICENSE*` / `COPYING*` / `NOTICE*` files in `licenses/`, and combines them in `THIRD_PARTY_NOTICES.txt`. It reports addons without a notice file. `trusscli info licenses` prints the same report without packaging. Collecting the files is not checking them: the licenses are yours to read, especially for libraries you add yourself (list their notices under `licenses` in `dist.json`).

Your own terms go in `EULA.txt`. Only the Windows installer asks for acceptance ("I Agree", with `installer.showEula`). Linux packages install `EULA.txt` and the notices under `/opt/<package>/`; Arch packages also link them from `/usr/share/licenses/<package>`.
