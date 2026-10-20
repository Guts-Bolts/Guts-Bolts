#!/usr/bin/env python3
"""
Guts and Bolts installer.

Double-click Install.bat (Windows), Install.command (macOS) or install.sh
(Linux) — or run `python3 install.py`. A window opens that:

  1. figures out which operating system you're on,
  2. installs the compiler and libraries the engine needs,
  3. downloads the rest of the engine's code and builds it,
  4. adds shortcuts for Guts and Bolts Studio and Guts&Bolts Player.

Options:
  --update    get the newest version from GitHub and rebuild
  --relaunch <program>   start this program when finished (used by the apps)
  --cli       use the text version instead of a window
  --check     just print what was detected and exit
  --build-dir <folder>   where to build (default: ./build)
  --no-shortcuts         don't create shortcuts
  --staff     (project owner) make this computer's account the official "Guts" account
  --server    start the Guts&Bolts server here (keeps accounts, Bolts and uploads in ./server_data)
  --port <n>  the server's port (default 7780)
"""

import getpass
import os
import platform
import queue
import shutil
import subprocess
import sys
import threading
import json
import time
import urllib.request
import zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent
APP_NAME = "Guts and Bolts"
REPO = "useofscript/Project-Guts-And-Bolts"
BRANCH = "main"

# ---------------------------------------------------------------------------
# Detecting the computer
# ---------------------------------------------------------------------------


class System:
    """What kind of computer we're on, in plain words."""

    def __init__(self):
        self.os = {"Windows": "windows", "Darwin": "macos"}.get(platform.system(), "linux")
        self.arch = platform.machine() or "unknown"
        self.pkg = None          # Linux package manager
        self.distro = ""
        if self.os == "windows":
            release = platform.release()
            try:
                if int(platform.version().split(".")[2]) >= 22000:
                    release = "11"
            except (IndexError, ValueError):
                pass
            self.pretty = f"Windows {release} ({self.arch})"
        elif self.os == "macos":
            chip = "Apple Silicon" if self.arch == "arm64" else "Intel"
            self.pretty = f"macOS {platform.mac_ver()[0]} ({chip})"
        else:
            self.distro = self._linux_name()
            for pm in ("apt-get", "dnf", "pacman", "zypper"):
                if shutil.which(pm):
                    self.pkg = pm
                    break
            self.pretty = f"{self.distro or 'Linux'} ({self.arch})"

    @staticmethod
    def _linux_name():
        try:
            for line in Path("/etc/os-release").read_text().splitlines():
                if line.startswith("PRETTY_NAME="):
                    return line.split("=", 1)[1].strip().strip('"')
        except OSError:
            pass
        return "Linux"


LINUX_PACKAGES = {
    "apt-get": ["build-essential", "cmake", "ninja-build", "git", "pkg-config",
                "libglfw3-dev", "libglew-dev", "libglm-dev", "libx11-dev"],
    "dnf":     ["gcc-c++", "cmake", "ninja-build", "git", "pkgconf-pkg-config",
                "glfw-devel", "glew-devel", "glm-devel", "libX11-devel"],
    "pacman":  ["base-devel", "cmake", "ninja", "git", "pkgconf", "glfw", "glew", "glm", "libx11"],
    "zypper":  ["gcc-c++", "cmake", "ninja", "git", "pkg-config",
                "libglfw-devel", "glew-devel", "glm-devel", "libX11-devel"],
}

MSYS_PACKAGES = ["mingw-w64-x86_64-gcc", "mingw-w64-x86_64-cmake", "mingw-w64-x86_64-ninja",
                 "mingw-w64-x86_64-glfw", "mingw-w64-x86_64-glew", "mingw-w64-x86_64-glm", "git"]

BREW_PACKAGES = ["cmake", "ninja", "glfw", "glew", "glm"]


class InstallError(Exception):
    """A problem we can explain to the person installing."""


# ---------------------------------------------------------------------------
# The installer steps
# ---------------------------------------------------------------------------


class Installer:
    def __init__(self, ui, build_dir=None, shortcuts=True, update=False, relaunch=None):
        self.update = update
        self.relaunch = relaunch
        self.ui = ui                     # has .log(text), .ask_password(prompt)
        self.sys = System()
        self.build_dir = Path(build_dir) if build_dir else ROOT / "build"
        self.shortcuts = shortcuts
        self.env = dict(os.environ)
        self.cmake_args = []
        self.msys = Path(os.environ.get("MSYS2_ROOT", r"C:\msys64"))

    # -- helpers --------------------------------------------------------------

    def find(self, program):
        """The full path of a program, looking in the folders we added (MSYS2, Homebrew, pip...)."""
        p = str(program)
        if os.path.dirname(p):
            return p
        return shutil.which(p, path=self.env.get("PATH")) or p

    def run(self, cmd, cwd=None, input_text=None, check=True):
        """Run a command, streaming its output into the log."""
        self.ui.log("$ " + " ".join(str(c) for c in cmd))
        # Windows only looks for programs in *this* program's PATH, not the one we
        # hand the new process, so tools we just installed (like MSYS2's cmake)
        # wouldn't be found. Use their full paths instead.
        cmd = [self.find(cmd[0])] + [str(c) for c in cmd[1:]]
        try:
            p = subprocess.Popen(cmd, cwd=cwd, env=self.env,
                                 stdin=subprocess.PIPE if input_text else subprocess.DEVNULL,
                                 stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                                 text=True, errors="replace")
        except FileNotFoundError:
            raise InstallError(f"Couldn't find the program '{Path(cmd[0]).name}'. "
                               "Try restarting your computer and running the installer again.")
        if input_text:
            p.stdin.write(input_text)
            p.stdin.close()
        for line in p.stdout:
            self.ui.log(line.rstrip())
        code = p.wait()
        if check and code != 0:
            raise InstallError(f"'{Path(str(cmd[0])).name}' stopped with an error (code {code}). "
                               "See the details below.")
        return code

    def have(self, program):
        return shutil.which(program, path=self.env.get("PATH")) is not None

    def steps(self):
        if self.update:
            return [
                ("Get the newest version", self.step_download),
                ("Check the tools", self.step_dependencies),
                ("Rebuild", self.step_build),
                ("Start it again", self.step_relaunch),
            ]
        return [
            ("Check this computer", self.step_check),
            ("Install compiler and libraries", self.step_dependencies),
            ("Download and build the engine", self.step_build),
            ("Add shortcuts", self.step_shortcuts),
        ]

    # -- updating ---------------------------------------------------------------

    @staticmethod
    def latest_commit():
        url = f"https://api.github.com/repos/{REPO}/commits/{BRANCH}"
        req = urllib.request.Request(url, headers={"Accept": "application/vnd.github+json",
                                                   "User-Agent": "GutsAndBolts-installer"})
        with urllib.request.urlopen(req, timeout=20) as r:
            return json.load(r)["sha"]

    def step_download(self):
        self.ui.log("Waiting a moment for the apps to close...")
        time.sleep(2)
        if (ROOT / ".git").exists() and shutil.which("git"):
            self.ui.log("Downloading the newest changes with git...")
            self.run(["git", "-C", ROOT, "pull", "--ff-only", "origin", BRANCH])
            return
        # Not a git folder (downloaded as a zip): grab the newest zip and unpack it over this one.
        url = f"https://github.com/{REPO}/archive/refs/heads/{BRANCH}.zip"
        tmp = ROOT / ".update.zip"
        self.ui.log(f"Downloading {url} ...")
        urllib.request.urlretrieve(url, tmp)
        with zipfile.ZipFile(tmp) as z:
            prefix = z.namelist()[0].split("/")[0] + "/"
            for member in z.infolist():
                rel = member.filename[len(prefix):]
                if not rel or rel.startswith("build/"):
                    continue
                target = ROOT / rel
                if member.is_dir():
                    target.mkdir(parents=True, exist_ok=True)
                else:
                    target.parent.mkdir(parents=True, exist_ok=True)
                    with z.open(member) as src, open(target, "wb") as dst:
                        shutil.copyfileobj(src, dst)
        tmp.unlink(missing_ok=True)
        self.remember_commit()
        self.ui.log("Unpacked the new version.")

    def remember_commit(self):
        """Zip installs have no git history: note which version this is for the update checker."""
        if (ROOT / ".git").exists():
            return
        try:
            (ROOT / ".gb_commit").write_text(self.latest_commit())
        except Exception as e:   # noqa: BLE001 - offline is fine
            self.ui.log(f"(Couldn't check the version number: {e})")

    def step_relaunch(self):
        if self.relaunch and Path(self.relaunch).exists():
            self.ui.log(f"Starting {Path(self.relaunch).name}...")
            self.launch(self.relaunch)
        else:
            self.ui.log("All up to date.")

    # -- 1. check -------------------------------------------------------------

    def step_check(self):
        s = self.sys
        self.ui.log(f"This computer: {s.pretty}")
        self.ui.log(f"Installing from: {ROOT}")
        if not (ROOT / "CMakeLists.txt").exists():
            raise InstallError("install.py must stay in the Guts and Bolts folder (next to CMakeLists.txt).")
        if s.os == "linux" and not s.pkg:
            raise InstallError("Couldn't find a package manager (apt, dnf, pacman or zypper). "
                               "Install cmake, ninja, git, a C++ compiler and the glfw, glew and glm "
                               "development packages yourself, then run this again.")
        if s.os == "windows" and s.arch.lower() not in ("amd64", "x86_64"):
            self.ui.log("Note: only 64-bit Intel/AMD Windows has been tested.")
        free = shutil.disk_usage(ROOT).free / 1e9
        self.ui.log(f"Free disk space: {free:.1f} GB")
        if free < 2:
            raise InstallError("You need about 2 GB of free disk space.")
        self.remember_commit()

    # -- 2. dependencies ------------------------------------------------------

    def step_dependencies(self):
        {"windows": self.deps_windows, "macos": self.deps_macos, "linux": self.deps_linux}[self.sys.os]()

    def deps_windows(self):
        bash = self.msys / "usr" / "bin" / "bash.exe"
        if not bash.exists():
            self.ui.log("MSYS2 (a free compiler toolkit for Windows) isn't installed yet - installing it...")
            if shutil.which("winget"):
                self.run(["winget", "install", "-e", "--id", "MSYS2.MSYS2",
                          "--accept-source-agreements", "--accept-package-agreements"], check=False)
            if not bash.exists():
                url = "https://github.com/msys2/msys2-installer/releases/latest/download/msys2-base-x86_64-latest.sfx.exe"
                sfx = Path(os.environ.get("TEMP", ".")) / "msys2-base.sfx.exe"
                self.ui.log("Downloading MSYS2...")
                urllib.request.urlretrieve(url, sfx)
                self.run([sfx, "-y", f"-o{self.msys.parent}"])
            if not bash.exists():
                raise InstallError(f"MSYS2 didn't install to {self.msys}. Install it from https://www.msys2.org "
                                   "and run this again.")
            # First start sets MSYS2 up and updates its core.
            self.run([bash, "-lc", "true"], check=False)
            self.run([bash, "-lc", "pacman --noconfirm -Syuu"], check=False)
            self.run([bash, "-lc", "pacman --noconfirm -Syuu"], check=False)
        self.ui.log("Installing the compiler, CMake and graphics libraries (this can take a few minutes)...")
        self.run([bash, "-lc", "pacman --noconfirm --needed -S " + " ".join(MSYS_PACKAGES)])
        mingw = self.msys / "mingw64" / "bin"
        self.env["PATH"] = f"{mingw};{self.msys / 'usr' / 'bin'};{self.env.get('PATH', '')}"
        self.env["MSYSTEM"] = "MINGW64"
        self.cmake_args = [f"-DCMAKE_PREFIX_PATH={mingw.parent.as_posix()}"]

    def deps_macos(self):
        if subprocess.run(["xcode-select", "-p"], capture_output=True).returncode != 0:
            subprocess.run(["xcode-select", "--install"])
            raise InstallError("Apple's developer tools are being installed. Click 'Install' in the "
                               "window that popped up, wait for it to finish, then run this installer again.")
        brew = shutil.which("brew") or next((b for b in ("/opt/homebrew/bin/brew", "/usr/local/bin/brew")
                                             if Path(b).exists()), None)
        if not brew:
            cmd = ('/bin/bash -c \\"$(curl -fsSL https://raw.githubusercontent.com/Homebrew/install/HEAD/install.sh)\\"')
            subprocess.run(["osascript", "-e", f'tell application "Terminal" to do script "{cmd}"',
                            "-e", 'tell application "Terminal" to activate'])
            raise InstallError("Homebrew (the Mac package manager) is being installed in a Terminal window. "
                               "Follow the steps there, then run this installer again.")
        self.env["PATH"] = f"{Path(brew).parent}:{self.env.get('PATH', '')}"
        self.run([brew, "install"] + BREW_PACKAGES)
        prefix = subprocess.run([brew, "--prefix"], capture_output=True, text=True).stdout.strip()
        if prefix:
            self.cmake_args = [f"-DCMAKE_PREFIX_PATH={prefix}"]

    def linux_already_ready(self):
        if not all(self.have(p) for p in ("cmake", "git", "pkg-config")):
            return False
        if not (self.have("c++") or self.have("g++") or self.have("clang++")):
            return False
        if not (self.have("ninja") or self.have("ninja-build")):
            return False
        ok = subprocess.run(["pkg-config", "--exists", "glfw3", "glew"], env=self.env).returncode == 0
        glm = any(Path(p).exists() for p in ("/usr/include/glm/glm.hpp", "/usr/local/include/glm/glm.hpp"))
        return ok and glm

    def deps_linux(self):
        if self.linux_already_ready():
            self.ui.log("Everything needed is already installed.")
            return
        pm = self.sys.pkg
        pkgs = LINUX_PACKAGES[pm]
        cmds = {
            "apt-get": [["apt-get", "update"], ["apt-get", "install", "-y"] + pkgs],
            "dnf":     [["dnf", "install", "-y"] + pkgs],
            "pacman":  [["pacman", "-S", "--needed", "--noconfirm"] + pkgs],
            "zypper":  [["zypper", "--non-interactive", "install"] + pkgs],
        }[pm]
        self.ui.log("Installing the compiler and libraries needs administrator rights.")
        if os.geteuid() == 0:
            for c in cmds:
                self.run(c)
            return
        password = self.ui.ask_password("Type your computer password to install the tools\n"
                                        "(it's only used for this and isn't saved):")
        if password is None:
            raise InstallError("Installation cancelled - the tools need your password to install.")
        for c in cmds:
            self.run(["sudo", "-S", "-p", ""] + c, input_text=password + "\n")

    # -- 3. build -------------------------------------------------------------

    def pip_tools(self):
        """Last resort: CMake and Ninja from Python's own package installer (works on every system)."""
        self.ui.log("CMake wasn't found, so installing it with Python's pip instead...")
        self.run([sys.executable, "-m", "pip", "install", "--user", "--upgrade", "cmake", "ninja"], check=False)
        import sysconfig
        folders = []
        try:
            folders.append(sysconfig.get_path("scripts", sysconfig.get_preferred_scheme("user")))
        except (AttributeError, KeyError):
            folders.append(sysconfig.get_path("scripts", "nt_user" if os.name == "nt" else "posix_user"))
        folders.append(sysconfig.get_path("scripts"))
        folders.append(str(Path(sys.executable).parent / "Scripts"))
        self.env["PATH"] = os.pathsep.join([f for f in folders if f] + [self.env.get("PATH", "")])

    def step_build(self):
        if not self.have("cmake"):
            self.pip_tools()
        if not self.have("cmake"):
            raise InstallError("CMake isn't available even after installing the tools. Try restarting "
                               "your computer and running the installer again, or install CMake from "
                               "https://cmake.org/download (tick 'Add CMake to the PATH').")
        generator = ["-G", "Ninja"] if (self.have("ninja") or self.have("ninja-build")) else []
        self.ui.log("Setting up the build (this downloads Dear ImGui, Lua and a few other parts)...")
        self.run(["cmake", "-S", ROOT, "-B", self.build_dir, "-DCMAKE_BUILD_TYPE=Release"]
                 + generator + self.cmake_args)
        self.ui.log("Building - this is the slow bit, grab a snack...")
        jobs = str(max(1, (os.cpu_count() or 2)))
        self.run(["cmake", "--build", self.build_dir, "--config", "Release", "--parallel", jobs])
        if not self.exe_path("GutsAndBolts").exists():
            raise InstallError("The build finished but the Guts and Bolts program wasn't made. See the details below.")
        if not self.exe_path("GutsAndBoltsPlayer").exists():
            self.ui.log("Note: this version doesn't include Guts&Bolts Player.")
        self.ui.log("Built successfully!")

    def exe_path(self, name):
        ext = ".exe" if self.sys.os == "windows" else ""
        for sub in ("", "Release"):
            p = self.build_dir / sub / (name + ext)
            if p.exists():
                return p
        return self.build_dir / (name + ext)

    # -- 4. shortcuts ---------------------------------------------------------

    def apps(self):
        return [(f"{APP_NAME} Studio", self.exe_path("GutsAndBolts")),
                ("Guts&Bolts Player", self.exe_path("GutsAndBoltsPlayer"))]

    def step_shortcuts(self):
        if not self.shortcuts:
            self.ui.log("Skipped (you unticked shortcuts).")
            return
        {"windows": self.shortcuts_windows, "macos": self.shortcuts_macos,
         "linux": self.shortcuts_linux}[self.sys.os]()

    def shortcuts_windows(self):
        desktop = Path(os.path.expandvars(r"%USERPROFILE%\Desktop"))
        menu = Path(os.path.expandvars(r"%APPDATA%\Microsoft\Windows\Start Menu\Programs")) / APP_NAME
        menu.mkdir(parents=True, exist_ok=True)
        for title, exe in self.apps():
            for folder in (desktop, menu):
                lnk = folder / f"{title.replace('&', ' and ')}.lnk"
                ps = (f"$s=(New-Object -ComObject WScript.Shell).CreateShortcut('{lnk}');"
                      f"$s.TargetPath='{exe}';$s.WorkingDirectory='{exe.parent}';"
                      f"$s.IconLocation='{exe},0';$s.Save()")
                self.run(["powershell", "-NoProfile", "-Command", ps], check=False)
        self.ui.log("Added shortcuts to the Desktop and Start menu.")

    def shortcuts_linux(self):
        apps_dir = Path.home() / ".local" / "share" / "applications"
        apps_dir.mkdir(parents=True, exist_ok=True)
        desktop = Path.home() / "Desktop"
        if shutil.which("xdg-user-dir"):
            found = subprocess.run(["xdg-user-dir", "DESKTOP"], capture_output=True, text=True).stdout.strip()
            if found:
                desktop = Path(found)
        for title, exe in self.apps():
            fname = title.lower().replace(" ", "-").replace("&", "-and-") + ".desktop"
            text = ("[Desktop Entry]\nType=Application\n"
                    f"Name={title}\nExec=\"{exe}\"\nPath={exe.parent}\n"
                    f"Icon={ROOT / 'Icon.png'}\nTerminal=false\nCategories=Game;Development;\n")
            for folder in (apps_dir, desktop):
                if not folder.exists():
                    continue
                f = folder / fname
                f.write_text(text)
                f.chmod(0o755)
                if folder == desktop and shutil.which("gio"):
                    subprocess.run(["gio", "set", str(f), "metadata::trusted", "true"], capture_output=True)
        self.ui.log("Added the apps to your applications menu and Desktop.")

    def shortcuts_macos(self):
        apps_dir = Path.home() / "Applications"
        apps_dir.mkdir(exist_ok=True)
        for title, exe in self.apps():
            app = apps_dir / f"{title}.app"
            macos = app / "Contents" / "MacOS"
            res = app / "Contents" / "Resources"
            macos.mkdir(parents=True, exist_ok=True)
            res.mkdir(parents=True, exist_ok=True)
            launcher = macos / "launch"
            launcher.write_text(f'#!/bin/bash\ncd "{exe.parent}"\nexec "{exe}"\n')
            launcher.chmod(0o755)
            (app / "Contents" / "Info.plist").write_text(
                '<?xml version="1.0" encoding="UTF-8"?>\n<plist version="1.0"><dict>'
                f"<key>CFBundleName</key><string>{title}</string>"
                "<key>CFBundleExecutable</key><string>launch</string>"
                "<key>CFBundleIconFile</key><string>icon</string>"
                "<key>CFBundlePackageType</key><string>APPL</string>"
                "<key>NSHighResolutionCapable</key><true/></dict></plist>\n")
            subprocess.run(["sips", "-s", "format", "icns", str(ROOT / "Icon.png"), "--out",
                            str(res / "icon.icns")], capture_output=True)
            self.ui.log(f"Created {app}")
        self.ui.log("Find the apps in your Applications folder (in your home folder).")

    # -- launching afterwards -------------------------------------------------

    def launch(self, exe):
        exe = Path(exe)
        if not exe.exists():
            return
        if self.sys.os == "windows":
            subprocess.Popen([str(exe)], cwd=exe.parent, env=self.env,
                             creationflags=getattr(subprocess, "DETACHED_PROCESS", 0))
        else:
            subprocess.Popen([str(exe)], cwd=exe.parent, start_new_session=True)


# ---------------------------------------------------------------------------
# Text version
# ---------------------------------------------------------------------------


class ConsoleUI:
    def log(self, text):
        print("   " + text, flush=True)

    def ask_password(self, prompt):
        try:
            return getpass.getpass(prompt.replace("\n", " ") + " ")
        except (EOFError, KeyboardInterrupt):
            return None


def run_console(args):
    ui = ConsoleUI()
    inst = Installer(ui, args.get("build_dir"), args.get("shortcuts", True), args.get("update", False),
                     args.get("relaunch"))
    verb = "Updating" if inst.update else "Installing"
    print(f"\n=== {verb} {APP_NAME} on {inst.sys.pretty} ===\n")
    steps = inst.steps()
    for i, (title, fn) in enumerate(steps, 1):
        print(f"[{i}/{len(steps)}] {title}")
        try:
            fn()
        except InstallError as e:
            print(f"\nX  {e}\n")
            return 1
        except Exception as e:   # noqa: BLE001 - show anything unexpected in plain words
            print(f"\nX  Something unexpected went wrong: {e}\n")
            return 1
    print("\nAll done! Start it with:")
    for title, exe in inst.apps():
        print(f"   {title}: {exe}")
    return 0


# ---------------------------------------------------------------------------
# Window version
# ---------------------------------------------------------------------------


def run_window(args):
    import tkinter as tk
    from tkinter import scrolledtext, simpledialog, ttk

    root = tk.Tk()
    root.title(f"Install {APP_NAME}")
    root.geometry("660x620")
    root.minsize(560, 520)
    events = queue.Queue()

    BG, CARD, TEXT, DIM, ORANGE, GREEN, RED = "#1f2128", "#2a2d36", "#e6e8ec", "#8a90a0", "#ff8c33", "#34b35a", "#e05555"
    root.configure(bg=BG)
    style = ttk.Style(root)
    try:
        style.theme_use("clam")
    except tk.TclError:
        pass
    style.configure("TProgressbar", troughcolor=CARD, background=ORANGE, bordercolor=CARD,
                    lightcolor=ORANGE, darkcolor=ORANGE)

    class WindowUI:
        def log(self, text):
            events.put(("log", text))

        def ask_password(self, prompt):
            answer = {}
            done = threading.Event()
            events.put(("password", (prompt, answer, done)))
            done.wait()
            return answer.get("value")

    ui = WindowUI()
    shortcuts_var = tk.BooleanVar(value=args.get("shortcuts", True))
    inst = Installer(ui, args.get("build_dir"), True, args.get("update", False), args.get("relaunch"))
    if inst.update:
        root.title(f"Updating {APP_NAME}")

    # --- Header ---
    header = tk.Frame(root, bg=BG)
    header.pack(fill="x", padx=20, pady=(18, 6))
    try:
        icon = tk.PhotoImage(file=str(ROOT / "Icon.png"))
        factor = max(1, icon.width() // 64)
        small = icon.subsample(factor, factor)
        root.iconphoto(True, icon)
        tk.Label(header, image=small, bg=BG).pack(side="left", padx=(0, 14))
        root._icon_refs = (icon, small)
    except tk.TclError:
        pass
    titles = tk.Frame(header, bg=BG)
    titles.pack(side="left", fill="x")
    tk.Label(titles, text=APP_NAME, font=("Segoe UI", 20, "bold"), fg=ORANGE, bg=BG).pack(anchor="w")
    tk.Label(titles, text="Game engine (Studio) + Guts&Bolts Player", font=("Segoe UI", 10), fg=DIM,
             bg=BG).pack(anchor="w")

    # --- Detected system ---
    info = tk.Frame(root, bg=CARD)
    info.pack(fill="x", padx=20, pady=8)
    tk.Label(info, text="Your computer", font=("Segoe UI", 9), fg=DIM, bg=CARD).pack(anchor="w", padx=12, pady=(8, 0))
    tk.Label(info, text=inst.sys.pretty, font=("Segoe UI", 12, "bold"), fg=TEXT, bg=CARD).pack(anchor="w", padx=12)
    how = {"windows": "Will use MSYS2 for the compiler and libraries.",
           "macos": "Will use Apple's developer tools and Homebrew.",
           "linux": f"Will use {inst.sys.pkg or 'your package manager'} to install the tools."}[inst.sys.os]
    tk.Label(info, text=how, font=("Segoe UI", 9), fg=DIM, bg=CARD).pack(anchor="w", padx=12, pady=(0, 8))

    # --- Steps ---
    steps_frame = tk.Frame(root, bg=BG)
    steps_frame.pack(fill="x", padx=20, pady=4)
    step_labels = []
    for title, _ in inst.steps():
        row = tk.Frame(steps_frame, bg=BG)
        row.pack(fill="x", pady=2)
        mark = tk.Label(row, text="○", width=2, font=("Segoe UI", 12), fg=DIM, bg=BG)
        mark.pack(side="left")
        lab = tk.Label(row, text=title, font=("Segoe UI", 11), fg=TEXT, bg=BG)
        lab.pack(side="left")
        step_labels.append(mark)

    progress = ttk.Progressbar(root, mode="determinate", maximum=len(step_labels))
    progress.pack(fill="x", padx=20, pady=(8, 4))
    status = tk.Label(root, text="Ready to install.", font=("Segoe UI", 10), fg=TEXT, bg=BG,
                      wraplength=600, justify="left")
    status.pack(anchor="w", padx=20)

    # --- Details log ---
    details = scrolledtext.ScrolledText(root, height=10, bg="#15171c", fg="#b8c0cc", insertbackground=TEXT,
                                        font=("Consolas", 9), relief="flat", wrap="word")
    details.pack(fill="both", expand=True, padx=20, pady=8)
    details.insert("end", f"Folder: {ROOT}\n")
    details.configure(state="disabled")

    # --- Buttons ---
    bottom = tk.Frame(root, bg=BG)
    bottom.pack(fill="x", padx=20, pady=(0, 16))
    tk.Checkbutton(bottom, text="Add Desktop / menu shortcuts", variable=shortcuts_var, bg=BG, fg=TEXT,
                   selectcolor=CARD, activebackground=BG, activeforeground=TEXT).pack(side="left")

    def button(parent, text, color, cmd):
        return tk.Button(parent, text=text, command=cmd, bg=color, fg="white", activebackground=color,
                         activeforeground="white", relief="flat", font=("Segoe UI", 11, "bold"),
                         padx=16, pady=6, cursor="hand2")

    buttons = tk.Frame(bottom, bg=BG)
    buttons.pack(side="right")
    close_btn = button(buttons, "Close", "#454a57", root.destroy)
    close_btn.pack(side="right", padx=(8, 0))
    install_btn = button(buttons, "Install", GREEN, lambda: start())
    install_btn.pack(side="right")

    def worker():
        inst.shortcuts = shortcuts_var.get()
        for i, (title, fn) in enumerate(inst.steps()):
            events.put(("step", (i, "run", title)))
            try:
                fn()
            except InstallError as e:
                events.put(("step", (i, "fail", str(e))))
                return
            except Exception as e:   # noqa: BLE001
                events.put(("step", (i, "fail", f"Something unexpected went wrong: {e}")))
                return
            events.put(("step", (i, "ok", title)))
        events.put(("done", None))

    def start():
        install_btn.configure(state="disabled", text="Installing...")
        for m in step_labels:
            m.configure(text="○", fg=DIM)
        progress["value"] = 0
        threading.Thread(target=worker, daemon=True).start()

    def finished():
        if inst.update:
            status.configure(text="Updated! Enjoy the new version.", fg=GREEN)
            root.after(1500, root.destroy)
            return
        status.configure(text="All done! Guts and Bolts is installed.", fg=GREEN)
        install_btn.pack_forget()
        button(buttons, "Open Player", "#3f6fd0", lambda: inst.launch(inst.exe_path("GutsAndBoltsPlayer"))
               ).pack(side="right", padx=(8, 0))
        button(buttons, "Open Studio", ORANGE, lambda: inst.launch(inst.exe_path("GutsAndBolts"))
               ).pack(side="right")

    def pump():
        try:
            while True:
                kind, data = events.get_nowait()
                if kind == "log":
                    details.configure(state="normal")
                    details.insert("end", data + "\n")
                    details.see("end")
                    details.configure(state="disabled")
                elif kind == "step":
                    i, state, text = data
                    if state == "run":
                        step_labels[i].configure(text="●", fg=ORANGE)
                        status.configure(text=text + "...", fg=TEXT)
                    elif state == "ok":
                        step_labels[i].configure(text="✓", fg=GREEN)
                        progress["value"] = i + 1
                    else:
                        step_labels[i].configure(text="✗", fg=RED)
                        status.configure(text=text, fg=RED)
                        install_btn.configure(state="normal", text="Try again")
                elif kind == "password":
                    prompt, answer, done = data
                    answer["value"] = simpledialog.askstring("Password needed", prompt, show="*", parent=root)
                    done.set()
                elif kind == "done":
                    finished()
        except queue.Empty:
            pass
        root.after(80, pump)

    pump()
    if inst.update:
        root.after(300, start)       # updating starts by itself
    root.mainloop()
    return 0


# ---------------------------------------------------------------------------


def parse_args(argv):
    args = {"shortcuts": True}
    it = iter(argv)
    for a in it:
        if a == "--cli":
            args["cli"] = True
        elif a == "--check":
            args["check"] = True
        elif a == "--no-shortcuts":
            args["shortcuts"] = False
        elif a == "--build-dir":
            args["build_dir"] = next(it, None)
        elif a == "--update":
            args["update"] = True
        elif a == "--relaunch":
            args["relaunch"] = next(it, None)
        elif a == "--staff":
            args["staff"] = True
        elif a == "--server":
            args["server"] = True
        elif a == "--port":
            args["port"] = next(it, None)
    return args


def make_staff_account(args):
    """Project owner only: make this computer's account the official "Guts" account."""
    inst = Installer(ConsoleUI(), build_dir=args.get("build_dir"))
    exe = inst.exe_path("GutsAndBoltsPlayer")
    if not exe.exists():
        print("Build Guts and Bolts first (run the installer), then try again.")
        return 1
    print("Opening Guts&Bolts Player to set up the staff account...")
    subprocess.call([str(exe), "--create-staff-account", "--page", "staff"], cwd=exe.parent)
    return 0


def run_server(args):
    """Start the Guts&Bolts server (accounts, Bolts, uploads) on this computer."""
    inst = Installer(ConsoleUI(), build_dir=args.get("build_dir"))
    exe = inst.exe_path("GutsAndBoltsServer")
    if not exe.exists():
        print("Build Guts and Bolts first (run the installer), then try again.")
        return 1
    data = Path(__file__).resolve().parent / "server_data"
    cmd = [str(exe), "--data", str(data)]
    if args.get("port"):
        cmd += ["--port", str(args["port"])]
    print("Starting the Guts&Bolts server. Keep this window open while people play.")
    print("Everything it stores goes in: " + str(data) + "\n")
    try:
        return subprocess.call(cmd, cwd=exe.parent)
    except KeyboardInterrupt:
        return 0


def main():
    args = parse_args(sys.argv[1:])
    if args.get("check"):
        s = System()
        print(f"OS: {s.os}\nDescription: {s.pretty}\nPackage manager: {s.pkg or '-'}")
        return 0
    if args.get("staff"):
        return make_staff_account(args)
    if args.get("server"):
        return run_server(args)
    if args.get("cli"):
        return run_console(args)
    try:
        import tkinter  # noqa: F401
        tkinter.Tk().destroy()
    except Exception:  # noqa: BLE001 - no window system / tkinter missing
        print("(No window available - using the text installer instead.)")
        return run_console(args)
    return run_window(args)


if __name__ == "__main__":
    sys.exit(main())
