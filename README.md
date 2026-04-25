# Audio Stem Exporter

Record any OBS audio source directly to **MP3, WAV, or AIFF** in real time — no conversion needed. Perfect for DJ mixes, stems, and multi-source sessions. **Can't stream and record video at the same time? Capture your audio stems instead — Audio Stem Exporter uses a fraction of the resources.**

> The only OBS plugin that writes MP3 directly. No post-processing, no DAW, no conversion step. Hit record, get an MP3.

> 🎧 **Perfect for DJ laptops** — Running Rekordbox, Serato, or Traktor while streaming on the same machine? There's no headroom left for video recording. Audio Stem Exporter captures your mix as a high-quality MP3 with barely any CPU overhead — so you never lose a set.

> 🎬 **Perfect for content creators** — Recording a stream or podcast? Capture each source as its own stem — mic, game audio, music, chat — so you can edit Reels, Shorts, and highlights with full audio control. No more stuck with a flat mix.

> 🖥️ **Can't stream and record video at the same time?** Audio Stem Exporter uses a fraction of the resources of video recording. No encoding, no heavy processing — if your PC or Mac can run OBS, it can run this. Capture your audio stems while you stream, no matter how old your hardware is.

---

## Screenshots

<p align="center">
  <img src="docs/images/stem-exporter-filter.png" alt="Filter panel showing active recording" width="600"/>
</p>
<p align="center">
  <img src="docs/images/stem-exporter-dock.png" alt="Dock panel showing multiple sources" width="300"/>
</p>

---

## Features

- **Direct to MP3** — no conversion, ever
- **WAV and AIFF** also supported
- **Works on any OBS audio source** — mic, browser, media, desktop audio
- **Record multiple sources simultaneously** — each gets its own file
- **Stack formats on one source** — add the filter twice to the same source to get MP3 and WAV at the same time, from a single recording pass
- **Follow Recording mode** — auto-starts and stops with OBS recording
- **Follow Streaming mode** — auto-starts and stops with OBS stream
- **Qt dock panel** — see all sources and control them from one place
- **Free** — no license, no account, no nonsense

---

## Download

| | File |
|---|---|
| **Windows Installer (recommended)** | [AudioStemExporter-Windows-x64-Setup.exe](https://github.com/NoUseForAnger/audio-stem-exporter/releases/latest) |
| **Windows ZIP (manual)** | [AudioStemExporter-Windows-x64.zip](https://github.com/NoUseForAnger/audio-stem-exporter/releases/latest) |
| **macOS PKG (recommended)** | [AudioStemExporter-macOS.pkg](https://github.com/NoUseForAnger/audio-stem-exporter/releases/latest) |
| **macOS ZIP (manual)** | [AudioStemExporter-macOS.zip](https://github.com/NoUseForAnger/audio-stem-exporter/releases/latest) |
| **Linux x86_64** | [AudioStemExporter-Linux-x86_64.zip](https://github.com/NoUseForAnger/audio-stem-exporter/releases/latest) |

**Requires:** OBS Studio 31+

---

## Installation

**Windows Installer:** Run the `.exe` — it auto-detects your OBS folder and installs everything.

**Windows ZIP:**
1. Copy `obs-plugins/64bit/obs-mp3-writer.dll` → your OBS `obs-plugins/64bit/` folder
2. Copy `data/obs-plugins/obs-mp3-writer/locale/en-US.ini` → same path in your OBS `data/` folder
3. Restart OBS

**macOS PKG (recommended):**
1. Download the `.pkg` file from the releases page
2. Double-click it and follow the installer — it handles everything including macOS security permissions
3. Restart OBS
4. The filter will appear under **Filters → Audio Stem Exporter**

**macOS ZIP (manual):**
1. Unzip the file
2. Copy the `obs-mp3-writer` folder to `/Library/Application Support/obs-studio/plugins/`
3. Open Terminal and run:
   ```
   sudo xattr -dr com.apple.quarantine "/Library/Application Support/obs-studio/plugins/obs-mp3-writer"
   ```
4. Restart OBS

> **Note:** Always use the PKG on macOS — it handles the security step automatically. The ZIP requires the Terminal command above or the plugin won't load.

---

## How to Use

### Filter (per source)
1. In OBS, right-click any audio source → **Filters**
2. Click **+** → **Audio Stem Exporter**
3. Choose your output folder and format (MP3, WAV, AIFF)
4. Set your trigger — Manual, Follow Recording, Follow Streaming, or Both
5. Click **Start Recording**

Your file saves automatically when you stop.

### Dock Panel
1. In OBS, go to **Docks** menu → **Audio Stem Exporter**
2. The dock shows all your sources that have the filter applied, their status, and format
3. Click **▶** next to any source to start recording that source individually
4. Click **■** to stop it
5. Use **Start All** / **Stop All** to control everything at once
6. Columns are resizable — drag the column edges to fit your layout

---

## FAQ

**Why is my MP3 file empty?**
Check that the audio source is actually routed and active in OBS — make sure it isn't muted and that the correct device is selected. Also make sure you added the filter to the right source.

**Can I record multiple sources at once?**
Yes — add the filter to each source. Each gets its own file. Use the dock panel to control them all from one place.

**Can I get multiple formats from the same source at the same time?**
Yes — add the filter more than once to the same source, set each instance to a different format. For example: one instance set to MP3 320kbps for archiving, another set to WAV for editing. Both record simultaneously from a single pass — no extra CPU cost for the audio capture itself.

**Where does the file get saved?**
Wherever you set the output folder in the filter settings. Default is your Videos folder (Windows) or Movies folder (Mac).

**Does it work while streaming?**
Yes — set the trigger to Follow Streaming and it auto-starts with your stream.

**The plugin installed but doesn't show up in OBS.**
The installer may have installed to the wrong OBS folder. If you have OBS in both `Program Files` and `Program Files (x86)`, the installer will now ask you which one to use. If you installed to the wrong location, run the installer again and choose the correct OBS folder — it should match where your OBS shortcut points.

**Is Mac supported?**
Yes — download the macOS PKG from the [releases page](https://github.com/NoUseForAnger/audio-stem-exporter/releases/latest). Always use the PKG, not the ZIP — the PKG handles macOS security automatically so the plugin shows up in OBS. See the Installation section above for full instructions.

**The plugin installed on Mac but doesn't show up in OBS.**
You likely used the ZIP instead of the PKG. Either reinstall using the PKG, or open Terminal and run:
```
sudo xattr -dr com.apple.quarantine "/Library/Application Support/obs-studio/plugins/obs-mp3-writer"
```
Then restart OBS.

---

## License

GPLv3 License — Copyright (c) 2026 1134 Digital LLC
See [LICENSE](LICENSE) for details. Commercial licensing available — contact [1134.digital](https://1134.digital).

---

Built with ❤️ by [Catch22](https://github.com/NoUseForAnger) · [1134.digital](https://1134.digital)
