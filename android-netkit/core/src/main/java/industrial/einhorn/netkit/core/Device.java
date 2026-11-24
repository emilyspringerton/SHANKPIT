package industrial.einhorn.netkit.core;

import java.util.ArrayList;
import java.util.List;

/** One host found on the LAN. ports = the interesting ports that answered (22, 5900, 80, ...). */
public final class Device {
    public String ip;
    public String mac = "";        // "" when unknown (ARP table unreadable)
    public String name = "";       // reverse-DNS / NetBIOS / mDNS best effort, "" if none
    public String guess = "";      // human guess: "Raspberry Pi", "Windows PC (VNC)", ...
    public final List<Integer> ports = new ArrayList<>();
    public Device(String ip) { this.ip = ip; }
}
