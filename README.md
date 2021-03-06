# PTest
PTest is a small program that simply sends timestamps over the NICs loopback.
PTest can send the packets these timestamps are in through TCP or UDP.
The point of PTest is to compare the two protocols while using `tc` to emulate certain network conditions.

# TC Primer
[tc](https://man7.org/linux/man-pages/man8/tc.8.html) is an incredibly complex tool used to shape network traffic going through a given device. It does so on the driver level allowing you to test how well your program does in degraded network environments without having to modify the program itself. It also has decades of research behind it to make the network behaviors are realistic as possible. For example: packet loss is almost never random. It typically happens in chunks. `tc` allows you to simply specifiy 10% packet loss and it'll emulate this chunking behavior itself!

In order to emulate packet loss:
- Find the name of your NIC on linux:
    - Use `ip link show` to list all devices. Commonly the device you want is enth0.
- Then you can use `tc` to emulate all sorts of network conditions. We're specifically interested in packet loss:
    - `sudo tc qdisc add dev <device_name> root netem loss 10%`
    - WARNING: Percents higher than 20 will tank an SSH connection.
- To restore your settings:
    - `sudo tc qdisc del dev <device_name> root`
 
# My Results
I used a VirtualBox VM running Ubuntu 18.0 LTS. The only test I've ran so far is 10% packet loss.
## With TCP
- Average latency: 259.367ms
- Average latency: 205.545ms
- Average latency: 78.757ms
- Average latency: 1204.24ms
- Average latency: 352.089ms

Due to TCPs retransmission behavior, the latency numbers on a connection with packet loss are wildly unpredictable.
## With UDP
- Average latency: 0ms and missing 116 packets.
- Average latency: 0ms and missing 114 packets.
- Average latency: 0ms and missing 92 packets.
- Average latency: 0ms and missing 91 packets.
- Average latency: 0ms and missing 97 packets.

PTest seends 1000 packets, so ~100 packets lost is expected.\
UDP can not only detect the packet loss, but doesn't skip a beat in its latency. It stays very consistent.

# How To Run
`ptest udp` to run UDP tests and `ptest tcp` to run tcp tests.

# Nagle's Algorithm
Another complexity of TCP is [Nagle's Algorithm](https://en.wikipedia.org/wiki/Nagle%27s_algorithm). This algorithm batches TCP packets on the sender's side if several packets are queued in quick succession. The main purpose of the algorithm was to smooth out SSH-like technologies. In these tests Nagle's is off by default to give TCP the best chance. However, you can run `ptest tcp 1` in order to enable Nagle's. The result is about 10ms added to TCPs latency.