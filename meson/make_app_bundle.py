"""Wrap the linked kiswaspg binary in a macOS .app bundle for macdeployqt."""
import pathlib
import shutil
import stat
import sys


def main() -> None:
    exe = pathlib.Path(sys.argv[1]).resolve()
    stamp = pathlib.Path(sys.argv[2]).resolve()
    version = sys.argv[3]
    app = exe.parent / 'kiswaspg.app'
    macos = app / 'Contents' / 'MacOS'
    macos.mkdir(parents=True, exist_ok=True)
    dest = macos / 'kiswaspg'
    shutil.copy2(exe, dest)
    dest.chmod(dest.stat().st_mode | stat.S_IXUSR | stat.S_IXGRP | stat.S_IXOTH)
    plist = f'''<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
  <key>CFBundleExecutable</key>
  <string>kiswaspg</string>
  <key>CFBundleIdentifier</key>
  <string>io.github.danale72.kiswaspg</string>
  <key>CFBundleName</key>
  <string>Kiswas PG</string>
  <key>CFBundlePackageType</key>
  <string>APPL</string>
  <key>CFBundleShortVersionString</key>
  <string>{version}</string>
  <key>CFBundleVersion</key>
  <string>{version}</string>
  <key>LSMinimumSystemVersion</key>
  <string>11.0</string>
  <key>NSHighResolutionCapable</key>
  <true/>
  <key>NSPrincipalClass</key>
  <string>NSApplication</string>
</dict>
</plist>
'''
    (app / 'Contents' / 'Info.plist').write_text(plist, encoding='utf-8')
    stamp.write_text('ok\n', encoding='utf-8')


if __name__ == '__main__':
    main()
