#!/bin/bash
# Turns a Linux server (Ubuntu 22.04 or 24.04, like a $5 cloud server) into a Guts&Bolts
# game server machine: it runs games by itself, so nobody has to host and nobody gets
# kicked when someone leaves. It starts by itself when the server reboots, and gets the
# newest version each time it starts.
#
# On the server, as root:
#   curl -fsSL https://raw.githubusercontent.com/useofscript/Project-Guts-And-Bolts/main/tools/game-server-setup.sh | bash
#
# The first time, it prints this machine's account ID. Give that account the Staff badge
# (in the Player: Staff page > search for the ID > Make Staff) and it starts running games
# within a minute. Run the line above again any time to check on it.
#   SLOTS=8 bash game-server-setup.sh     run up to 8 games at once (4 if not given)
set -euo pipefail

SLOTS="${SLOTS:-4}"
HOME_DIR=/opt/gutsandbolts            # the program
DATA_DIR=/var/lib/gutsandbolts        # its account key (keep it: it's who the machine is)
DOWNLOAD=https://gutsandbolts.net/download/GutsAndBolts-Linux.tar.gz

if [ "$(id -u)" != 0 ]; then echo "Run this as root (or put sudo in front)."; exit 1; fi

echo "== Installing what it needs"
export DEBIAN_FRONTEND=noninteractive
apt-get update -qq
apt-get install -y -qq curl ca-certificates libglfw3 libgl1 > /dev/null
apt-get install -y -qq libglew2.2 > /dev/null 2>&1 || apt-get install -y -qq libglew2.1 > /dev/null

id -u gutsandbolts > /dev/null 2>&1 || useradd --system --home "$DATA_DIR" --shell /usr/sbin/nologin gutsandbolts
mkdir -p "$HOME_DIR" "$DATA_DIR"
chown gutsandbolts: "$DATA_DIR"
chmod 700 "$DATA_DIR"

# Fetches the newest Guts&Bolts for Linux (the service runs this each time it starts).
cat > "$HOME_DIR/update.sh" <<EOF
#!/bin/bash
set -e
cd "$HOME_DIR"
if curl -fsSL --retry 3 -o new.tar.gz "$DOWNLOAD"; then
  rm -rf new && mkdir new && tar xzf new.tar.gz -C new --strip-components=1
  cp new/GutsAndBoltsGameServer GutsAndBoltsGameServer.new && mv GutsAndBoltsGameServer.new GutsAndBoltsGameServer
  rm -rf new new.tar.gz
else
  echo "Couldn't download the newest version; using the one already here."
fi
EOF
chmod 755 "$HOME_DIR/update.sh"
echo "== Downloading Guts&Bolts"
"$HOME_DIR/update.sh"
chmod 755 "$HOME_DIR/GutsAndBoltsGameServer"

cat > /etc/systemd/system/gutsandbolts-games.service <<EOF
[Unit]
Description=Guts&Bolts game server machine
After=network-online.target
Wants=network-online.target

[Service]
User=gutsandbolts
Environment=GB_ACCOUNT_DIR=$DATA_DIR
WorkingDirectory=$DATA_DIR
ExecStartPre=+$HOME_DIR/update.sh
ExecStart=$HOME_DIR/GutsAndBoltsGameServer --slots $SLOTS
Restart=always
RestartSec=30

[Install]
WantedBy=multi-user.target
EOF

ACCOUNT=$(runuser -u gutsandbolts -- env GB_ACCOUNT_DIR="$DATA_DIR" HOME="$DATA_DIR" "$HOME_DIR/GutsAndBoltsGameServer" --id)
systemctl daemon-reload
systemctl enable --now gutsandbolts-games.service > /dev/null 2>&1
systemctl restart gutsandbolts-games.service

echo
echo "== Done. It's running, and starts by itself when this server reboots."
echo
echo "This machine's account ID:"
echo "  $ACCOUNT"
echo
echo "If you haven't yet: in the Guts&Bolts Player, open the Staff page, search for"
echo "  ${ACCOUNT:0:12}"
echo "and press Make Staff. It starts running games within a minute."
echo
echo "See what it's doing:  journalctl -u gutsandbolts-games -f"
echo "Stop it:              systemctl stop gutsandbolts-games"
