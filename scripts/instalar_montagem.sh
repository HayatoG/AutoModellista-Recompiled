#!/bin/sh
# Instala scripts/montar_volumes.sh no disco interno (~/.local/bin/am-montar) e um agente do launchd que o roda a
# cada 60 s: quando o SSD USB cai e volta, as imagens AMRecomp e DecompAM remontam sozinhas.
# Desfazer: launchctl bootout gui/$(id -u)/com.automodellista.montar; rm ~/Library/LaunchAgents/com.automodellista.montar.plist
set -e
here=$(cd "$(dirname "$0")" && pwd)
bin="$HOME/.local/bin/am-montar"
plist="$HOME/Library/LaunchAgents/com.automodellista.montar.plist"
mkdir -p "$HOME/.local/bin" "$HOME/Library/LaunchAgents" "$HOME/Library/Logs"
cp "$here/montar_volumes.sh" "$bin"
chmod 755 "$bin"
cat > "$plist" <<EOF
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
  <key>Label</key><string>com.automodellista.montar</string>
  <key>ProgramArguments</key><array><string>$bin</string></array>
  <key>StartInterval</key><integer>60</integer>
  <key>RunAtLoad</key><true/>
  <key>StandardOutPath</key><string>$HOME/Library/Logs/am-montar.log</string>
  <key>StandardErrorPath</key><string>$HOME/Library/Logs/am-montar.log</string>
</dict>
</plist>
EOF
launchctl bootout "gui/$(id -u)/com.automodellista.montar" 2>/dev/null || true
launchctl bootstrap "gui/$(id -u)" "$plist"
echo "instalado: $bin e $plist (log em ~/Library/Logs/am-montar.log)"
