# SD-card preset (cloud-init) for the Router Pi

Goal: flash Ubuntu Server 64-bit to the SD card, edit two files on the card's boot
partition (`system-boot`, FAT, readable on the iMac/any PC), insert it in the Pi, power on.
First boot then: creates your login, enables SSH, and joins the **phone's Wi-Fi hotspot as
a client** (and/or the phone's USB tether) so the Pi has internet for `apt`. Then you run
`setup-router.sh` once over SSH and it replaces the Wi-Fi-client setup with the access point.

Image: "Ubuntu Server 24.04 LTS (64-bit)" for Raspberry Pi 3 (Raspberry Pi Imager ->
Other general-purpose OS -> Ubuntu). Pi Imager's own "OS customisation" (gear) screen can do
the same thing and is fine; the files below are the documented, repeatable form. If you
use the gear screen, do not also edit these files.

Status: written from the documented cloud-init NoCloud / netplan formats for Ubuntu
Server on Pi. **Not booted on real hardware in this session** -- treat first boot as the test.

## Secrets rule

Nothing below contains a real password or Wi-Fi passphrase. Replace the `CHANGE-ME` tokens
on the card only. Do not commit the edited files. Prefer an SSH public key over a password.

## `user-data` (on the boot partition, replaces the existing file; first line must be `#cloud-config`)

```yaml
#cloud-config
hostname: router-pi
manage_etc_hosts: true
timezone: UTC            # your zone, e.g. America/New_York

users:
  - name: pi-admin       # login preset: choose your own name
    groups: [sudo]
    shell: /bin/bash
    sudo: ALL=(ALL) NOPASSWD:ALL
    lock_passwd: true    # password login off; SSH key only (recommended)
    ssh_authorized_keys:
      - ssh-ed25519 AAAA...CHANGE-ME-your-public-key

ssh_pwauth: false
# LOCKOUT WARNING: with lock_passwd + ssh_pwauth off, a wrong/missing public key above means no
# login at all (reflash the card). Paste the key carefully; test it on the first boot.
# If you really need a password instead, remove lock_passwd/ssh_pwauth above and add
#   chpasswd: {expire: false, users: [{name: pi-admin, password: CHANGE-ME, type: text}]}
#   ssh_pwauth: true
# (cloud-init stores the hash; the plaintext sits on the card until first boot -- wipe it after.)

# Regulatory domain so Wi-Fi is usable on first boot.
write_files:
  - path: /etc/default/crda
    content: |
      REGDOMAIN=US     # keep equal to COUNTRY in router.conf
  - path: /etc/modprobe.d/cfg80211-regdom.conf
    content: |
      options cfg80211 ieee80211_regdom=US

runcmd:
  - [ rfkill, unblock, wifi ]
  - [ systemctl, enable, --now, ssh ]
```

Ubuntu Server for Pi already has `openssh-server`; the `runcmd` line is belt and braces.

## `network-config` (on the boot partition; netplan v2 syntax)

```yaml
version: 2
wifis:
  wlan0:
    dhcp4: true
    optional: true
    regulatory-domain: US          # same as COUNTRY in router.conf
    access-points:
      "CHANGE-ME-phone-hotspot-SSID":
        password: "CHANGE-ME-phone-hotspot-passphrase"
ethernets:
  # USB tethering from the phone (RNDIS/CDC) -- also works for the first apt run.
  usb-tether-a:
    match: {name: "usb0"}
    dhcp4: true
    optional: true
  usb-tether-b:
    match: {name: "enx*"}
    dhcp4: true
    optional: true
```

Notes:

- For the first boot you need only ONE of the two uplinks: the phone's Wi-Fi hotspot
  (phone: hotspot ON, 2.4 GHz band) **or** the phone plugged into the Pi with USB tethering ON.
  Using USB tethering for setup is better: your SSH session then does not ride on `wlan0`,
  which the script is about to turn into an access point.
- `optional: true` stops boot waiting for a link that is not there (headless Pi).
- The Pi has no screen: find it by `ssh pi-admin@router-pi.local` (needs mDNS on your
  laptop) or by looking at the phone's connected-devices list.
- `setup-router.sh` retires cloud-init's generated `50-cloud-init.yaml` and writes
  `/etc/cloud/cloud.cfg.d/99-router-pi-disable-network-config.cfg` so cloud-init does not
  restore the client Wi-Fi on later boots.
- Pi 3 B v2.1 is 2.4 GHz only. Pin the phone hotspot to 2.4 GHz if it offers the choice.
