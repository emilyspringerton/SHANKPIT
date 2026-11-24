# Router Pi (NETKIT Deliverable 2)

A Raspberry Pi 3 Model B v2.1 on 64-bit Ubuntu Server that gives the iMac internet from an
Android phone, with no screen needed on the Pi:

```
 Android phone --USB cable (tethering: usb0 / enx*)--> Pi 3 B --wlan0 (2.4 GHz AP, WPA2)--> iMac
   (mobile data)                                    DHCP+DNS+NAT    same SSID/passphrase as
                                                  fixed lease by MAC   the phone hotspot
```

Files:

| File | What |
|---|---|
| `setup-router.sh` | The one idempotent setup script. `--dry-run`, `--check`/`--status`. |
| `router.conf.example` | Config template (SSID, passphrase, country, channel, subnet, iMac MAC/IP). Copy to `router.conf` (gitignored). |
| `sd-card-cloud-init.md` | `user-data` / `network-config` snippets for the SD card preset. |

## Build order (steps 1 and 5-6 live elsewhere; this directory covers 2-4)

Step 1 is the iMac's `setup.bat` (VNC server, Deliverable 1).

**Step 2 -- write the SD card.** Flash Ubuntu Server 24.04 64-bit (Pi 3 image) with Raspberry
Pi Imager. Then edit `user-data` and `network-config` on the card's `system-boot` partition
as in `sd-card-cloud-init.md` (login preset + first-boot uplink to the phone). No secrets are
hardcoded in the docs; fill them in on the card only.

**Step 3 -- first boot and setup, once.**
1. Phone: USB tethering ON (or hotspot ON), insert the SD card, power the Pi (plug the phone
   into one of the Pi's USB ports if using tethering). Wait ~2 minutes for first boot.
2. `ssh pi-admin@<pi address>`; get the files onto the Pi (`scp -r router-pi pi-admin@...:`).
3. `cp router.conf.example router.conf`, edit: SSID + passphrase (same as the phone hotspot),
   `COUNTRY`, `IMAC_MAC` (the iMac's **Wi-Fi** MAC, from the iMac's Network settings; if the
   OS uses a private/randomised Wi-Fi address, turn that off for this network), `IMAC_IP`.
4. `sudo ./setup-router.sh` -- installs hostapd/dnsmasq/nftables, writes all config, enables the
   services. If your SSH session is over `wlan0` it defers going live (it would cut you off; detected via `ss`, since `sudo` strips `SSH_CONNECTION`):
   `sudo reboot`. If the AP then fails to come up and you have no tether, use the built-in Ethernet. Over USB tether it applies immediately. `--apply-now` / `--no-apply` / `--reboot`
   override. Re-running is safe: unchanged files are left alone, and with nothing changed and all services
   active it does not restart anything (so it will not drop the iMac or your SSH session); to change settings, edit
   `router.conf` and run it again.
5. Rehearse without touching anything: `./setup-router.sh --dry-run` renders every file into a
   temp dir for inspection (needs no root, works on any Linux).

**Step 4 -- verify.** `sudo ./setup-router.sh --check` prints PASS/FAIL per item (tether
interface, default route, internet, wlan0 address, AP mode, hostapd/dnsmasq/nftables active
and enabled, ip_forward, NAT rule persisted, DHCP/DNS listening, iMac lease). Exit code is 0 only
with zero FAILs. Done = the iMac joins the SSID by itself and loads a web page via the phone.
Then power-cycle the Pi once with no keyboard/screen and run `--check` again: that proves the
boot ordering.

## What the script does (and why)

- Network stack: Ubuntu Server's netplan + systemd-networkd (`/etc/netplan/60-router-pi.yaml`):
  `wlan0` static `LAN_IP` (`ignore-carrier`, since there is no carrier until hostapd is up);
  `usb0` and `enx*` DHCP clients with route metric 100 and `optional` (boot never blocks). The
  built-in `eth0` gets a DHCP fallback at metric 200 (a wired maintenance path if the AP ever
  fails; the phone tether stays the preferred uplink). Needs `LAN_IP`'s subnet not to collide with
  the phone's tether subnet (Android often uses 192.168.42.x / 192.168.x.x: change `LAN_IP` if so).
- Access point: `hostapd`, 2.4 GHz `hw_mode=g`, 802.11n, WPA2-PSK/CCMP, `country_code`. A tiny
  `router-pi-wlan.service` runs `rfkill unblock wifi` + `iw reg set` before hostapd; drop-ins
  give hostapd/dnsmasq `Restart=on-failure` and ordering after the wlan0 device and networkd.
- DHCP/DNS: `dnsmasq` with `bind-dynamic` + `interface=wlan0`, so it never collides with
  `systemd-resolved`'s stub on `127.0.0.53:53` (resolved is left running; dnsmasq uses its
  upstream list from `/run/systemd/resolve/resolv.conf`, plus `DNS_FALLBACK`). Fixed lease for the iMac by MAC.
- NAT: `nftables` (`/etc/nftables.conf`, loaded by `nftables.service` at boot):
  masquerade out of anything but `wlan0`, forward policy drop except wlan0 -> out + established,
  TCP MSS clamping (tethering links often have a smaller MTU). Optional `TETHER_TTL_FIX`.
- `net.ipv4.ip_forward=1` via `/etc/sysctl.d/`.
- Requires internet at run time (`apt`). Files it replaces are backed up once as `*.router-pi.orig`.

## Troubleshooting order: check the phone, then the router Pi

1. **The phone** (most failures are here):
   - Mobile data on, signal, no data-saver blocking tethering.
   - USB cable is a data cable (a charge-only cable shows no interface at all).
   - USB tethering toggled ON *after* plugging in (Settings -> Network -> Hotspot & tethering).
     It switches off whenever the cable is unplugged or the phone reboots.
   - Carrier tethering allowance/throttling (see `TETHER_TTL_FIX`); phone's own Wi-Fi hotspot
     OFF if you want the iMac to use the Pi (same SSID, so the iMac may pick the phone instead).
2. **The router Pi**, in this order, using `sudo ./setup-router.sh --check`:
   - tether interface missing / no IPv4 / no default route -> `ip -br link`, `dmesg | tail`,
     `journalctl -u systemd-networkd -b`; replug, toggle tethering, reboot the Pi once.
   - internet fails but the tether is up -> phone data problem (back to step 1) or DNS.
   - AP not in AP mode / hostapd failed -> `journalctl -u hostapd -b`, `rfkill list`, wrong `COUNTRY`
     or `CHANNEL`; check `iw reg get`.
   - dnsmasq failed -> `journalctl -u dnsmasq -b` (port 53/67 conflict, wlan0 address missing).
   - iMac has no lease -> wrong `IMAC_MAC` (or private Wi-Fi address), wrong passphrase on the iMac,
     iMac cached the phone hotspot instead (forget it); `sudo cat /var/lib/misc/dnsmasq.leases`.
3. Only then the iMac (`setup.bat` / VNC, Deliverable 1).

## Honest status

Verified on the dev box: `bash -n`; `--dry-run` renders all 10 files, a second dry-run reports
every file unchanged (idempotent render); config validation rejects placeholder SSID, out-of-subnet
IMAC_IP, bad MAC; `--check` runs and reports FAIL on a non-Pi host. `shellcheck` was not installed
here so it was not run. **Not verified on real hardware**: apt install, netplan apply, hostapd on the
Pi 3's brcmfmac, RNDIS/CDC interface names with the actual phone, nft rule load (`nft -c` not
run -- nftables absent in the sandbox), boot ordering, iMac join. The first real run is the test;
report failures with `--check` output and `journalctl -b -u hostapd -u dnsmasq`.
Netplan `ignore-carrier` needs netplan >= 0.103 (Ubuntu 22.04+/24.04 are fine).
Stopgap label: this is OS-setup shell, not PARENA (see header of `setup-router.sh`).
Review pass additions (also unverified on hardware): `dnsmasq --test` and `nft -c -f` run before
anything is restarted; `no-resolv` was dropped from the dnsmasq config; the stale-TTL nft table is
removed when `TETHER_TTL_FIX` goes back to 0; leading-zero IPs are rejected.
