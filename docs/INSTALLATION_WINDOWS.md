# Running TrussC Installations on Windows

A checklist for PCs that run a TrussC app unattended for days or weeks: exhibitions, signage, museum pieces. Windows' defaults are made for a desk PC. Left alone, they can turn the display off, restart the machine at night, block the exe, or keep a crashed app on screen behind a dialog.

Almost everything here is configuration, not code. The registry and power commands change the whole machine and need an administrator prompt.

For Linux boxes (Raspberry Pi and other SBCs), see [GET_STARTED_CONSOLE_MODE.md](GET_STARTED_CONSOLE_MODE.md).

---

## 1. Smart App Control (Windows 11)

Smart App Control starts in an evaluation mode and can switch itself to enforcement days or weeks after setup. From then on it blocks unsigned executables, including your app. The error reads "... was blocked by your organization's Device Guard policy", and SmartScreen's "Run anyway" does not help.

- **Check:** Windows Security → App & browser control → Smart App Control. If it is not **Off**, decide now, not on site.
- **Turn it off:** set it to **Off** and restart. Turning it off may be one-way: turning it back on can require reinstalling Windows. Defender and SmartScreen keep running.
- **Diagnose a block:** Event Viewer → Applications and Services Logs → Microsoft → Windows → CodeIntegrity → Operational. Event 3077 records the block.
- In the long run, code-signing the exe avoids the problem.

## 2. Power and display

Fullscreen alone does **not** keep the display on, and nothing in TrussC prevents system sleep by default. Use the High performance plan and set the timeouts to never:

```
powercfg /setactive SCHEME_MIN
powercfg /change monitor-timeout-ac 0
powercfg /change standby-timeout-ac 0
powercfg /hibernate off
```

- Turn the screensaver off: Settings → Personalization → Lock screen → Screen saver.
- Turn notifications off, or enable Do not disturb: Settings → System → Notifications.
- In the app, call `setKeepScreenOn(true)` in `setup()` as a second layer. It asks Windows to keep the display and the system awake while the app runs. Call it from the main thread.
- The High performance plan also keeps Windows from moving a hidden or minimized app to the efficiency cores.
- **No High performance plan?** Many current laptops and mini PCs (Modern Standby) have only the Balanced plan, and the first line fails with an error such as "The power scheme, subgroup or setting specified does not exist". Check with `powercfg /l`. Stay on Balanced and set Settings → System → Power (or Power & battery) → Power mode → **Best performance**. The other three lines still apply.

## 3. Windows Update

Windows Update installs updates and restarts the PC on its own, usually at night, outside the "active hours".

- Settings → Windows Update → Advanced options → **Active hours**: cover the opening hours, or pause updates for the duration of the show.
- Plan for the restart anyway: the PC must sign in and start the app again by itself (section 5).
- After an unplanned restart, the app starts from scratch. If it needs to remember anything (calibration, counters), save it to a file as it changes rather than only in `exit()`.

## 4. Crashes: no dialog, keep a dump

**Suppress the crash dialog.** Depending on WER settings, a "... has stopped working" dialog can appear. It keeps the dead process alive until someone clicks it, so a watchdog that restarts the app when it exits never fires.

```
reg add "HKLM\SOFTWARE\Microsoft\Windows\Windows Error Reporting" /v DontShowUI /t REG_DWORD /d 1 /f
```

**Write a crash dump.** WER LocalDumps writes a dump for your exe without any code change:

```
reg add "HKLM\SOFTWARE\Microsoft\Windows\Windows Error Reporting\LocalDumps\MyApp.exe" /v DumpFolder /t REG_EXPAND_SZ /d "C:\CrashDumps" /f
reg add "HKLM\SOFTWARE\Microsoft\Windows\Windows Error Reporting\LocalDumps\MyApp.exe" /v DumpType /t REG_DWORD /d 2 /f
reg add "HKLM\SOFTWARE\Microsoft\Windows\Windows Error Reporting\LocalDumps\MyApp.exe" /v DumpCount /t REG_DWORD /d 10 /f
```

**Keep the symbols.** A dump is only readable with the matching `.pdb`. `trusscli build` and VS Code builds are RelWithDebInfo by default, so the `.pdb` sits next to the `.exe` in `bin/`. The Visual Studio solution (`--ide vs`) opens in Debug: pick RelWithDebInfo (or Release) in the configuration drop-down before you build an exe to deploy. A Debug exe needs Visual Studio's debug runtime and won't start on a PC without Visual Studio. Archive the `.exe` and `.pdb` together for every build you deploy. Open the `.dmp` in Visual Studio (or WinDbg) with that `.pdb` to see the call stack.

**Reading the Event Log.** Event Viewer → Windows Logs → Application → "Application Error" (event 1000) names the faulting module and offset. An entry with `ucrtbase.dll` and `0xc0000409` usually means an uncaught C++ exception or `abort()`, not a buffer overrun.

## 5. Auto-start and restart

1. **Automatic sign-in:** use Sysinternals Autologon, or `netplwiz`, so the PC reaches the desktop after a restart without anyone typing a password.
2. **Start the app at sign-in:** create a Task Scheduler task with the trigger "At log on". In the action, set **"Start in"** to the folder that contains the exe (see section 6). Then change two defaults, or Windows stops the task:
   - **Settings** tab: uncheck **"Stop the task if it runs longer than"**. It is on by default with 3 days, so Windows ends the app (or the supervisor that restarts it) about 72 hours after sign-in.
   - **Conditions** tab: uncheck **"Start the task only if the computer is on AC power"**. This matters on laptops.
   - Without the GUI: in the task's XML, `<ExecutionTimeLimit>PT0S</ExecutionTimeLimit>` means no limit. In PowerShell, pass `New-ScheduledTaskSettingsSet -ExecutionTimeLimit ([TimeSpan]::Zero) -AllowStartIfOnBatteries -DontStopIfGoingOnBatteries` as `-Settings` to `Register-ScheduledTask`.
   - The task's process runs at priority 7 (below normal) by default. If that matters, set `<Priority>` in the XML, or `-Priority` on `New-ScheduledTaskSettingsSet`.
3. **Restart it when it stops:**
   - **anchorbolt** ([tettou771/anchorbolt](https://github.com/tettou771/anchorbolt)): `anchorbolt start` supervises a TrussC app: it restarts the app when it exits or stops responding, collects the logs, and can report to a fleet dashboard. Start anchorbolt from the scheduled task instead of the app.
   - **Without anchorbolt:** a small loop script that restarts the exe when it exits works, as long as the crash dialog is suppressed (section 4).

## 6. Working directory and file paths

A Task Scheduler task with an empty "Start in" runs with the working directory at `C:\Windows\System32`, and shortcuts can differ too. Don't depend on the working directory:

- Set "Start in" to the exe folder in the task or shortcut.
- Load assets with paths under `bin/data`, the way the examples do. `getDataPath("file")` gives the absolute path.
- Give the log file an absolute path, e.g. `getLogger().setLogFile(getDataPath("app.log"))`, or an absolute `TRUSSC_LOG_FILE`. `setLogFile()` does not create missing folders: for a subfolder such as `logs/`, create it first (`fs::create_directories(getDataPath("logs"))`). Check the return value: `setLogFile()` returns false when it can't open the file, and a Release or RelWithDebInfo app has no console to show the error.

## 7. GPU on dual-GPU machines

On laptops and PCs with both an integrated and a discrete GPU, the app may run on the integrated one. Go to Settings → System → Display → Graphics, add the exe, and choose **High performance**.

## 8. Before you leave the site

Run through this once on the actual machine:

- [ ] Leave the app running with no input for longer than the old display timeout: the display stays on.
- [ ] End the app in Task Manager: it comes back (section 5).
- [ ] Restart Windows: it signs in and the app starts.
- [ ] The task's "Stop the task if it runs longer than" setting is off (Settings tab, section 5).
- [ ] Force a crash in a test build: a `.dmp` appears in the dump folder, and no dialog stays on screen.
- [ ] The log file is written where you expect it.
- [ ] Smart App Control is off, or the exe is signed.
