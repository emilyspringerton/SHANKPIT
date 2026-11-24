#!/usr/bin/env bash
# setup-router.sh -- turn a Raspberry Pi 3 B (64-bit Ubuntu) into the NETKIT router:
#   uplink  : Android phone, USB tethering (usb0 / enx*) on the Pi's USB port
#   downlink: wlan0 as a 2.4 GHz WPA2 access point, DHCP + DNS, fixed lease for the iMac
#   NAT     : nftables masquerade, persisted via nftables.service
#
# STOPGAP (CLAUDE.md "Core Deps Are PARENA-First"): this is OS-setup shell, allowed by the
# repo convention, but it is shell and not PARENA. A PARENA replacement for the config
# rendering/validation belongs with PARENA/stdlib/netkit; hostapd/dnsmasq/nftables
# themselves stay as OS components.
#
# Usage:
#   sudo ./setup-router.sh [--config router.conf]      apply (idempotent, safe to re-run)
#   ./setup-router.sh --dry-run [--stage DIR]          render all files into DIR, touch nothing
#   sudo ./setup-router.sh --check                     pass/fail report (alias: --status)
# Options: --apply-now  force live network apply even over an SSH-on-wlan0 session
#          --no-apply   write + enable everything, do not start/restart (use reboot)
#          --reboot     reboot at the end of a real run
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
CONF_FILE="$SCRIPT_DIR/router.conf"
MODE="apply"; DRY=0; STAGE=""; APPLY_NOW=0; NO_APPLY=0; DO_REBOOT=0
INSTALLED_CONF="/etc/router-pi/router.conf"
MARK="# Managed by router-pi/setup-router.sh -- edits are overwritten on re-run; change router.conf instead."

usage() { sed -n '2,18p' "$0" | sed 's/^# \{0,1\}//'; exit "${1:-0}"; }
while [ $# -gt 0 ]; do
  case "$1" in
    --config) CONF_FILE="${2:?--config needs a file}"; shift ;;
    --dry-run) DRY=1 ;;
    --stage) STAGE="${2:?--stage needs a dir}"; shift ;;
    --check|--status) MODE="check" ;;
    --apply-now) APPLY_NOW=1 ;;
    --no-apply) NO_APPLY=1 ;;
    --reboot) DO_REBOOT=1 ;;
    -h|--help) usage 0 ;;
    *) echo "unknown option: $1" >&2; usage 1 ;;
  esac
  shift
done
if [ "$DRY" = 1 ] && [ -z "$STAGE" ]; then STAGE="$(mktemp -d /tmp/router-pi-stage.XXXXXX)"; fi
if [ "$DRY" = 0 ] && [ -n "$STAGE" ]; then echo "--stage only makes sense with --dry-run" >&2; exit 1; fi

log()  { printf '[router-pi] %s\n' "$*"; }
warn() { printf '[router-pi] WARN: %s\n' "$*" >&2; }
die()  { printf '[router-pi] ERROR: %s\n' "$*" >&2; exit 1; }

# ---------------------------------------------------------------- config
load_conf() {
  [ -f "$CONF_FILE" ] || die "config not found: $CONF_FILE (copy router.conf.example to router.conf and edit)"
  case "$CONF_FILE" in */*) ;; *) CONF_FILE="./$CONF_FILE" ;; esac   # '.' would search $PATH for a bare name
  # shellcheck disable=SC1090
  . "$CONF_FILE"
  : "${SSID:?}" "${PASSPHRASE:?}" "${COUNTRY:?}" "${CHANNEL:?}" "${LAN_IP:?}" "${LAN_PREFIX_LEN:?}"
  : "${DHCP_START:?}" "${DHCP_END:?}" "${IMAC_MAC:?}" "${IMAC_IP:?}"
  DHCP_LEASE="${DHCP_LEASE:-12h}"; IMAC_HOSTNAME="${IMAC_HOSTNAME:-imac}"; HIDDEN_SSID="${HIDDEN_SSID:-0}"
  TETHER_IFACE="${TETHER_IFACE:-}"; DNS_FALLBACK="${DNS_FALLBACK:-1.1.1.1 9.9.9.9}"; TETHER_TTL_FIX="${TETHER_TTL_FIX:-0}"
  IMAC_MAC="$(printf '%s' "$IMAC_MAC" | tr 'A-F' 'a-f')"
}
ip2int() { local IFS=.; local a b c d; read -r a b c d <<<"$1"; echo $(( (10#$a<<24)+(10#$b<<16)+(10#$c<<8)+10#$d )); }  # 10#: no octal surprise on 010
int2ip() { echo "$(( ($1>>24)&255 )).$(( ($1>>16)&255 )).$(( ($1>>8)&255 )).$(( $1&255 ))"; }
valid_ip() { local o; [[ "$1" =~ ^[0-9]+\.[0-9]+\.[0-9]+\.[0-9]+$ ]] || return 1; for o in ${1//./ }; do [[ "$o" =~ ^(0|[1-9][0-9]{0,2})$ ]] && [ "$o" -le 255 ] || return 1; done; }  # no leading zeros: inet_aton reads 050 as octal
validate_conf() {
  [[ "$SSID" != CHANGE-ME* && "$PASSPHRASE" != CHANGE-ME* ]] || die "edit SSID/PASSPHRASE in $CONF_FILE (still the CHANGE-ME placeholders)"
  local sl; sl="$(printf '%s' "$SSID" | wc -c)"   # bytes, not characters (non-ASCII SSIDs)
  [ "$sl" -ge 1 ] && [ "$sl" -le 32 ] || die "SSID must be 1..32 bytes"
  [ "${#PASSPHRASE}" -ge 8 ] && [ "${#PASSPHRASE}" -le 63 ] || die "PASSPHRASE must be 8..63 characters (WPA2)"
  [[ "$COUNTRY" =~ ^[A-Z]{2}$ ]] || die "COUNTRY must be a 2-letter uppercase code"
  [[ "$CHANNEL" =~ ^([1-9]|1[0-3])$ ]] || die "CHANNEL must be 1..13 (2.4 GHz only on a Pi 3 B v2.1)"
  [[ "$IMAC_MAC" =~ ^([0-9a-f]{2}:){5}[0-9a-f]{2}$ ]] || die "IMAC_MAC must look like aa:bb:cc:dd:ee:ff"
  [ "$IMAC_MAC" != "aa:bb:cc:dd:ee:ff" ] || die "IMAC_MAC is still the example value"
  local v; for v in LAN_IP DHCP_START DHCP_END IMAC_IP; do valid_ip "${!v}" || die "$v is not a valid IPv4 address"; done
  [[ "$LAN_PREFIX_LEN" =~ ^[0-9]+$ ]] && [ "$LAN_PREFIX_LEN" -ge 16 ] && [ "$LAN_PREFIX_LEN" -le 30 ] || die "LAN_PREFIX_LEN must be 16..30"
  MASKI=$(( (0xFFFFFFFF << (32-LAN_PREFIX_LEN)) & 0xFFFFFFFF )); MASK="$(int2ip "$MASKI")"
  NETI=$(( $(ip2int "$LAN_IP") & MASKI )); NET="$(int2ip "$NETI")"; SUBNET="$NET/$LAN_PREFIX_LEN"
  for v in DHCP_START DHCP_END IMAC_IP; do
    [ $(( $(ip2int "${!v}") & MASKI )) -eq "$NETI" ] || die "$v (${!v}) is outside $SUBNET"
  done
  [ "$IMAC_IP" != "$LAN_IP" ] || die "IMAC_IP equals LAN_IP"
  [ "$(ip2int "$DHCP_START")" -le "$(ip2int "$DHCP_END")" ] || die "DHCP_START > DHCP_END"
  [[ "$HIDDEN_SSID" =~ ^[01]$ && "$TETHER_TTL_FIX" =~ ^[01]$ ]] || die "HIDDEN_SSID and TETHER_TTL_FIX must be 0 or 1"
  [[ "$SSID$PASSPHRASE" != *$'\n'* && "$SSID$PASSPHRASE" != *$'\r'* ]] || die "SSID/PASSPHRASE may not contain line breaks"
}

# ---------------------------------------------------------------- tether detection
detect_tether() {  # prints the first usb tether interface name, else nothing
  local d n drv
  [ -n "$TETHER_IFACE" ] && { [ -e "/sys/class/net/$TETHER_IFACE" ] && echo "$TETHER_IFACE"; return 0; }
  for d in /sys/class/net/*; do
    n="$(basename "$d")"
    case "$n" in lo|wlan*|wl*) continue ;; esac
    drv=""; [ -L "$d/device/driver" ] && drv="$(basename "$(readlink "$d/device/driver")")"
    case "$n:$drv" in usb0:*|enx*:*|*:rndis_host|*:cdc_ether|*:cdc_ncm|*:ipheth) echo "$n"; return 0 ;; esac
  done
  return 0
}

# ---------------------------------------------------------------- check mode
do_check() {
  local fails=0 warns=0
  pass() { printf '  [PASS] %s\n' "$*"; }
  fail() { printf '  [FAIL] %s\n' "$*"; fails=$((fails+1)); }
  note() { printf '  [WARN] %s\n' "$*"; warns=$((warns+1)); }
  load_conf; validate_conf
  echo "router-pi check ($SUBNET, SSID '$SSID')"
  local t; t="$(detect_tether || true)"
  if [ -n "$t" ]; then
    pass "tether interface present: $t"
    ip -4 addr show dev "$t" 2>/dev/null | grep -q 'inet ' && pass "$t has an IPv4 address (phone gave DHCP)" || fail "$t has no IPv4 address -- phone: is USB tethering switched on?"
    ip -4 route show default dev "$t" 2>/dev/null | grep -q . && pass "default route via $t" || fail "no default route via $t"
    ping -c1 -W4 -I "$t" 1.1.1.1 >/dev/null 2>&1 && pass "internet reachable through $t (ping 1.1.1.1)" || fail "no internet through $t -- check the phone's mobile data"
    getent hosts example.com >/dev/null 2>&1 && pass "DNS resolves on the Pi" || note "DNS lookup failed on the Pi"
  else
    fail "no usb tether interface (usb0/enx*) -- phone: USB cable data-capable? USB tethering on? Pi unplugged/replugged?"
  fi
  ip link show wlan0 >/dev/null 2>&1 && pass "wlan0 exists" || fail "wlan0 missing"
  rfkill list wifi 2>/dev/null | grep -qi 'blocked: yes' && fail "wifi is rfkill-blocked" || pass "wifi not rfkill-blocked"
  ip -4 addr show dev wlan0 2>/dev/null | grep -q "inet $LAN_IP/$LAN_PREFIX_LEN" && pass "wlan0 has $LAN_IP/$LAN_PREFIX_LEN" || fail "wlan0 lacks $LAN_IP/$LAN_PREFIX_LEN"
  iw dev wlan0 info 2>/dev/null | grep -q 'type AP' && pass "wlan0 is in AP mode" || fail "wlan0 is not in AP mode"
  iw dev wlan0 info 2>/dev/null | grep -q 'channel' && pass "AP: $(iw dev wlan0 info 2>/dev/null | grep -m1 channel | sed 's/^[[:space:]]*//')" || true
  local s; for s in hostapd dnsmasq nftables systemd-networkd; do
    systemctl is-active --quiet "$s" && pass "$s active" || fail "$s not active (journalctl -u $s -b)"
    systemctl is-enabled --quiet "$s" && pass "$s enabled at boot" || fail "$s not enabled at boot"
  done
  [ "$(sysctl -n net.ipv4.ip_forward 2>/dev/null)" = 1 ] && pass "net.ipv4.ip_forward=1" || fail "ip_forward is 0"
  nft list table ip router_pi_nat 2>/dev/null | grep -q masquerade && pass "nftables masquerade rule loaded" || fail "nftables masquerade rule missing"
  grep -q 'router_pi' /etc/nftables.conf 2>/dev/null && pass "NAT persisted in /etc/nftables.conf" || fail "NAT not persisted"
  ss -ulnH 2>/dev/null | grep -q "$LAN_IP:67\|0.0.0.0:67\|\*:67" && pass "DHCP server listening on :67" || fail "nothing listening on UDP 67"
  ss -ulnH 2>/dev/null | grep -q "$LAN_IP:53" && pass "DNS listening on $LAN_IP:53 (no clash with resolved stub)" || fail "dnsmasq not listening on $LAN_IP:53"
  if [ -f /var/lib/misc/dnsmasq.leases ] && grep -qi "$IMAC_MAC" /var/lib/misc/dnsmasq.leases; then
    pass "iMac has a lease: $(grep -i "$IMAC_MAC" /var/lib/misc/dnsmasq.leases | awk '{print $3}')"
  else note "iMac ($IMAC_MAC) has no lease yet -- not joined, or Wi-Fi MAC differs from IMAC_MAC (iMac may use a private/random Wi-Fi address)"; fi
  echo "result: $fails fail, $warns warn"
  [ "$fails" -eq 0 ]
}

# ---------------------------------------------------------------- file writing
declare -A CHANGED=()
run() { if [ "$DRY" = 1 ]; then log "(dry-run) $*"; else "$@"; fi; }
write_file() {  # write_file PATH MODE  (content on stdin)
  local path="$1" mode="$2" tmp target
  target="$STAGE$path"; tmp="$(mktemp)"; cat >"$tmp"
  mkdir -p "$(dirname "$target")"
  if [ -f "$target" ] && cmp -s "$tmp" "$target"; then
    log "unchanged  $path"; rm -f "$tmp"; chmod "$mode" "$target" 2>/dev/null || true; return 0
  fi
  if [ "$DRY" = 0 ] && [ -f "$target" ] && ! grep -qF "Managed by router-pi" "$target" && [ ! -e "$target.router-pi.orig" ]; then
    cp -p "$target" "$target.router-pi.orig"; log "backed up original -> $path.router-pi.orig"
  fi
  install -m "$mode" "$tmp" "$target"; rm -f "$tmp"; CHANGED["$path"]=1; log "wrote      $path"
}

render_all() {
  local ssid_hidden=0; [ "$HIDDEN_SSID" = 1 ] && ssid_hidden=1
  local tether_yaml
  if [ -n "$TETHER_IFACE" ]; then
    tether_yaml="    tether:
      match: {name: \"$TETHER_IFACE\"}
      dhcp4: true
      dhcp4-overrides: {route-metric: 100, use-dns: true}
      dhcp6: false
      optional: true"
  else
    tether_yaml="    tether-usb0:
      match: {name: \"usb0\"}
      dhcp4: true
      dhcp4-overrides: {route-metric: 100, use-dns: true}
      dhcp6: false
      optional: true
    tether-enx:
      match: {name: \"enx*\"}
      dhcp4: true
      dhcp4-overrides: {route-metric: 100, use-dns: true}
      dhcp6: false
      optional: true"
  fi

  write_file /etc/router-pi/router.conf 600 <"$CONF_FILE"

  # wlan0 static address (networkd renderer; works on Ubuntu Server). ignore-carrier:
  # the link has no carrier until hostapd is up, but the address must be there anyway.
  write_file /etc/netplan/60-router-pi.yaml 600 <<EOF
$MARK
network:
  version: 2
  renderer: networkd
  ethernets:
    wlan0:
      addresses: [$LAN_IP/$LAN_PREFIX_LEN]
      dhcp4: false
      dhcp6: false
      ignore-carrier: true
      optional: true
    # Built-in Ethernet: keep a DHCP fallback (a wired SSH/maintenance path that survives a
    # misconfigured AP), at a worse metric than the phone tether so tethering stays the uplink.
    eth0:
      dhcp4: true
      dhcp4-overrides: {route-metric: 200, use-dns: true}
      dhcp6: false
      optional: true
$tether_yaml
EOF

  write_file /etc/cloud/cloud.cfg.d/99-router-pi-disable-network-config.cfg 644 <<EOF
$MARK
# Stop cloud-init regenerating the first-boot Wi-Fi-client netplan over the AP setup.
network: {config: disabled}
EOF

  write_file /etc/hostapd/hostapd.conf 600 <<EOF
$MARK
interface=wlan0
driver=nl80211
ssid=$SSID
ignore_broadcast_ssid=$ssid_hidden
country_code=$COUNTRY
ieee80211d=1
hw_mode=g
channel=$CHANNEL
ieee80211n=1
wmm_enabled=1
auth_algs=1
wpa=2
wpa_key_mgmt=WPA-PSK
rsn_pairwise=CCMP
wpa_passphrase=$PASSPHRASE
macaddr_acl=0
EOF

  write_file /etc/dnsmasq.d/router-pi.conf 644 <<EOF
$MARK
# bind-dynamic + a single interface: dnsmasq only claims wlan0's own addresses, so it
# never fights systemd-resolved's stub listener on 127.0.0.53:53.
interface=wlan0
except-interface=lo
bind-dynamic
domain-needed
bogus-priv
# Upstream DNS = whatever the phone's DHCP gave systemd-resolved (changes per network).
# (no "no-resolv" here: it can make dnsmasq ignore resolv-file; resolv-file alone replaces
# /etc/resolv.conf, which is the stub 127.0.0.53 and would loop.)
resolv-file=/run/systemd/resolve/resolv.conf
$(for s in $DNS_FALLBACK; do echo "server=$s"; done)
dhcp-authoritative
dhcp-range=$DHCP_START,$DHCP_END,$MASK,$DHCP_LEASE
dhcp-option=option:router,$LAN_IP
dhcp-option=option:dns-server,$LAN_IP
dhcp-host=$IMAC_MAC,$IMAC_IP,$IMAC_HOSTNAME
dhcp-leasefile=/var/lib/misc/dnsmasq.leases
EOF

  write_file /etc/sysctl.d/99-router-pi.conf 644 <<EOF
$MARK
net.ipv4.ip_forward=1
EOF

  # the mangle table is always created+deleted so turning TETHER_TTL_FIX off also removes a loaded one
  local ttl='
table ip router_pi_mangle
delete table ip router_pi_mangle'
  [ "$TETHER_TTL_FIX" = 1 ] && ttl="$ttl"'
table ip router_pi_mangle {
  chain post {
    type filter hook postrouting priority mangle; policy accept;
    oifname != "wlan0" oifname != "lo" ip ttl set 65
  }
}'
  write_file /etc/nftables.conf 644 <<EOF
#!/usr/sbin/nft -f
$MARK
# add+delete first makes reloading idempotent without flushing other people's rules.
table ip router_pi_nat
delete table ip router_pi_nat
table ip router_pi_nat {
  chain postrouting {
    type nat hook postrouting priority srcnat; policy accept;
    ip saddr $SUBNET oifname != "wlan0" oifname != "lo" masquerade
  }
}
table inet router_pi_filter
delete table inet router_pi_filter
table inet router_pi_filter {
  chain forward {
    type filter hook forward priority filter; policy drop;
    ct state established,related accept
    iifname "wlan0" oifname != "wlan0" tcp flags syn tcp option maxseg size set rt mtu
    iifname "wlan0" oifname != "wlan0" accept
  }
}$ttl
EOF

  write_file /etc/systemd/system/router-pi-wlan.service 644 <<EOF
$MARK
[Unit]
Description=Router Pi: unblock Wi-Fi and set regulatory domain before hostapd
After=sys-subsystem-net-devices-wlan0.device
BindsTo=sys-subsystem-net-devices-wlan0.device
Before=hostapd.service

[Service]
Type=oneshot
RemainAfterExit=yes
ExecStart=/usr/sbin/rfkill unblock wifi
ExecStart=/usr/sbin/iw reg set $COUNTRY

[Install]
WantedBy=multi-user.target
EOF

  write_file /etc/systemd/system/hostapd.service.d/router-pi.conf 644 <<EOF
$MARK
[Unit]
Wants=router-pi-wlan.service
After=router-pi-wlan.service systemd-networkd.service sys-subsystem-net-devices-wlan0.device
StartLimitIntervalSec=0

[Service]
Restart=on-failure
RestartSec=5
EOF

  write_file /etc/systemd/system/dnsmasq.service.d/router-pi.conf 644 <<EOF
$MARK
[Unit]
Wants=hostapd.service
After=hostapd.service systemd-networkd.service systemd-resolved.service
StartLimitIntervalSec=0

[Service]
Restart=on-failure
RestartSec=5
EOF
}

# ---------------------------------------------------------------- apply
# Is any SSH session riding on wlan0? (sudo strips SSH_CONNECTION, so ask the kernel via ss.)
ssh_on_wlan0() {
  local peers p
  peers="$(ss -tnH state established '( sport = :22 )' 2>/dev/null | awk '{sub(/:[0-9]+$/,"",$4); print $4}' || true)"
  [ -z "${SSH_CONNECTION:-}" ] || peers="$peers"$'\n'"$(awk '{print $1}' <<<"$SSH_CONNECTION")"
  while IFS= read -r p; do
    p="${p#[}"; p="${p%]}"; p="${p#::ffff:}"
    [ -n "$p" ] || continue
    case "$p" in *%wlan0) return 0 ;; esac
    if ip route get "${p%%\%*}" 2>/dev/null | grep -q 'dev wlan0'; then return 0; fi
  done <<<"$peers"
  return 1
}

apply_all() {
  [ "$DRY" = 1 ] || [ "$(id -u)" -eq 0 ] || die "run as root (sudo ./setup-router.sh)"
  local pkgs=(hostapd dnsmasq nftables iw rfkill wireless-regdb)
  if [ "$DRY" = 1 ] || ! dpkg -s "${pkgs[@]}" >/dev/null 2>&1; then
    log "installing packages: ${pkgs[*]} (needs internet: first-boot Wi-Fi client or USB tether)"
    if [ "$DRY" = 0 ]; then
      # keep dpkg from auto-starting dnsmasq/hostapd with default config mid-install
      printf '#!/bin/sh\nexit 101\n' >/usr/sbin/policy-rc.d; chmod +x /usr/sbin/policy-rc.d
      trap 'rm -f /usr/sbin/policy-rc.d' EXIT
      DEBIAN_FRONTEND=noninteractive apt-get update -qq
      DEBIAN_FRONTEND=noninteractive apt-get install -y "${pkgs[@]}"
      rm -f /usr/sbin/policy-rc.d; trap - EXIT; CHANGED[pkgs]=1
    else log "(dry-run) apt-get install -y ${pkgs[*]}"; fi
  else log "packages already installed"; fi
  # Some Ubuntu Pi kernels ship RNDIS/CDC and brcmfmac in the -extra modules package.
  if [ "$DRY" = 0 ] && ! modinfo rndis_host >/dev/null 2>&1 && apt-cache show linux-modules-extra-raspi >/dev/null 2>&1; then
    DEBIAN_FRONTEND=noninteractive apt-get install -y linux-modules-extra-raspi || warn "could not install linux-modules-extra-raspi"
    NEED_REBOOT=1
  fi
  if [ "$DRY" = 0 ] && systemctl is-active --quiet NetworkManager 2>/dev/null; then
    warn "NetworkManager is active; this script targets Ubuntu Server (networkd). Remove NetworkManager or expect conflicts."
  fi

  render_all

  # Debian/Ubuntu's dnsmasq init script already loads /etc/dnsmasq.d itself; do NOT also add a
  # conf-dir line to dnsmasq.conf (the file would load twice -> duplicate dhcp-host error).
  # Syntax-check before anything is restarted.
  if [ "$DRY" = 0 ]; then
    dnsmasq --test -C /etc/dnsmasq.d/router-pi.conf >/dev/null 2>&1 || { dnsmasq --test -C /etc/dnsmasq.d/router-pi.conf || true; die "dnsmasq rejected /etc/dnsmasq.d/router-pi.conf (see above); nothing was started"; }
    nft -c -f /etc/nftables.conf || die "nft rejected /etc/nftables.conf (see above); nothing was started"
    if command -v ufw >/dev/null 2>&1 && ufw status 2>/dev/null | grep -q 'Status: active'; then
      warn "ufw is active: its forward policy will block LAN->tether traffic; run 'ufw disable' or allow routed traffic from wlan0"
    fi
  fi
  # retire the first-boot Wi-Fi-client netplan (cloud-init's) so wlan0 is not also a client
  if [ -f "$STAGE/etc/netplan/50-cloud-init.yaml" ] && [ "$DRY" = 0 ]; then
    mv /etc/netplan/50-cloud-init.yaml /etc/netplan/50-cloud-init.yaml.disabled-by-router-pi; CHANGED[retire]=1; log "retired /etc/netplan/50-cloud-init.yaml"
  elif [ "$DRY" = 1 ]; then log "(dry-run) would retire /etc/netplan/50-cloud-init.yaml if present"; fi

  run modprobe -q rndis_host || true; run modprobe -q cdc_ether || true
  run systemctl daemon-reload
  run systemctl unmask hostapd
  run systemctl enable systemd-networkd router-pi-wlan.service hostapd dnsmasq nftables
  run sysctl -q -p /etc/sysctl.d/99-router-pi.conf

  # Nothing changed and everything already running: do not bounce hostapd (that would drop
  # the iMac -- and any SSH session riding on the AP -- on every re-run).
  local live=1 u
  if [ "$DRY" = 0 ] && [ "${#CHANGED[@]}" -eq 0 ] && [ "${NEED_REBOOT:-0}" = 0 ]; then
    live=0; for u in router-pi-wlan hostapd dnsmasq nftables; do systemctl is-active --quiet "$u" || live=1; done
    [ "$live" = 1 ] || log "nothing changed and all services active: no restart"
  fi
  # Applying wlan0 -> AP kills an SSH session that rides on wlan0 (the first-boot client link).
  local defer=$NO_APPLY
  if [ "$live" = 1 ] && [ "$DRY" = 0 ] && [ "$APPLY_NOW" = 0 ] && ssh_on_wlan0; then
    defer=1; log "an SSH session is on wlan0: deferring live apply (reboot, or rerun with --apply-now)"
  fi
  if [ "$live" = 0 ]; then :
  elif [ "$defer" = 1 ]; then
    log "files written and services enabled; they start on next boot"
  else
    run netplan generate
    run netplan apply
    run systemctl restart router-pi-wlan.service
    run systemctl restart hostapd
    run systemctl restart dnsmasq
    run systemctl restart nftables
  fi
  if [ "$DRY" = 1 ]; then
    log "dry-run done; rendered files are under $STAGE"
  else
    log "done. Verify with: sudo $0 --check   (after the phone is plugged in with USB tethering ON)"
    if [ "${NEED_REBOOT:-0}" = 1 ]; then log "kernel modules were installed: reboot required"; fi
    if [ "$DO_REBOOT" = 1 ]; then log "rebooting"; systemctl reboot; fi
  fi
}

if [ "$MODE" = check ] && [ ! -f "$CONF_FILE" ] && [ -f "$INSTALLED_CONF" ]; then CONF_FILE="$INSTALLED_CONF"; fi
load_conf
if [ "$MODE" = check ]; then do_check; exit $?; fi
validate_conf
apply_all
