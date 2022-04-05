#include <iostream>
#include <iomanip>
#include <atomic>
#include <thread>
#include <chrono>
#include <cstring>

#include <fcntl.h>
#include <sys/socket.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <poll.h>
#include <linux/tcp.h>

namespace TCP_test {
int serverWaitForConnection() {
  int sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);

  int flags = fcntl(sock, F_GETFL, 0);
  flags |= O_NONBLOCK;
  fcntl(sock, F_SETFL, flags);

  sockaddr_in service;
  service.sin_family = AF_INET;
  service.sin_addr.s_addr = INADDR_ANY;
  service.sin_port = htons(50000);

  int a = bind(sock, (sockaddr *)&service, sizeof(sockaddr_in));
  int b = listen(sock, SOMAXCONN);

  socklen_t psize = sizeof(sockaddr_in);
  sockaddr_in incoming;

  std::cout << "Awaiting Connections..." << std::endl;

  std::atomic<bool> continueService(true);

  while (continueService) {
    int ret = accept(sock, (sockaddr *)&incoming, &psize);

    if (ret == -1) {
      if (errno == EWOULDBLOCK) {
        continue;
      } else {
        std::cout << "Closing time" << std::endl;
        break;
      }
    }

    if (ret == 0)
      continue;

    return ret;
  }

  return 0;
}

void runBenchmarkOn(int _sock) {
  struct pollfd fds[1];
  fds[0].fd = _sock;
  fds[0].events = POLLIN | POLLERR;

  int totalLat = 0;
  int count = 0;

  int offset = 0;
  char buff[1500] = {0};
  char pool[15000] = {0};

  auto start = std::chrono::system_clock::now();
  while (true) {
    int pollRC = poll(fds, 1, 100);
    if (pollRC > 0) {
      int sze = recv(_sock, buff, sizeof(buff), 0);

      if (sze == -1 || sze == 0) {
        std::cout << "sze is " << sze << std::endl;
        break;
      }

      std::memcpy(pool + offset, buff, sze);
      offset += sze;

      auto now = std::chrono::system_clock::now();
      auto diff = now - start;
      auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(diff);
      static int counter = 0;

      for (int i = 0; i < offset - 4; ++i) {
        // Find special marker
        if (pool[i + 0] == (char)15 && pool[i + 1] == (char)16 &&
            pool[i + 2] == (char)15 && pool[i + 3] == (char)16) {
          // Parse timestamp
          decltype(now) timestamp =
              *(decltype(now) *)(pool + i - sizeof(decltype(now)));

          auto latency = now - timestamp;
          auto latms =
              std::chrono::duration_cast<std::chrono::milliseconds>(latency);
          std::cout << "(" << std::setw(5) << (ms.count()) << ") ["
                    << std::setw(3) << counter++ << "] Received packet with "
                    << latms.count() << "ms latency." << std::endl;

          totalLat += latms.count();
          count += 1;

          char temp[sizeof(pool)];
          std::memcpy(temp, pool + (i + 4), sizeof(pool) - (i + 4));
          std::memcpy(pool, temp, sizeof(pool));
          offset -= (i + 4);
          i = 0;
        }
      }
    }
  }

  std::cout << "Average latency: " << (totalLat/(float)count) << "ms" << std::endl;
}

void clientBenchmark(bool withNagels = false) {
  int sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);

  if (!withNagels) {
    int flag = 1;
    int result = setsockopt(sock,          /* socket affected */
                            IPPROTO_TCP,   /* set option at TCP level */
                            TCP_NODELAY,   /* name of option */
                            (char *)&flag, /* the cast is historical cruft */
                            sizeof(int));  /* length of option value */
  }

  sockaddr_in service;
  service.sin_family = AF_INET;
  service.sin_addr.s_addr = inet_addr("127.0.0.1");
  service.sin_port = htons(50000);

  if (connect(sock, (sockaddr *)&service, sizeof(sockaddr_in)) != -1) {
    for (int i = 0; i < 1000; ++i) {
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
      char buff[1500] = {0};
      auto now = std::chrono::system_clock::now();
      std::memcpy(buff, &now, sizeof(now));

      buff[sizeof(now) + 0] = (char)15;
      buff[sizeof(now) + 1] = (char)16;
      buff[sizeof(now) + 2] = (char)15;
      buff[sizeof(now) + 3] = (char)16;
      send(sock, buff, sizeof(buff), 0);
    }
    close(sock);
  } else {
    std::cout << "Couldn't connect" << std::endl;
  }
}

void run(bool nagles) {
  std::thread t([nagles]() {
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    clientBenchmark(nagles);
  });

  int sock = serverWaitForConnection();
  if (sock) {
    runBenchmarkOn(sock);
  }

  t.join();
}

} // namespace TCP_test

namespace UDP_test {
int serverWaitForConnection() {
  int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);

  int flags = fcntl(sock, F_GETFL, 0);
  flags |= O_NONBLOCK;
  fcntl(sock, F_SETFL, flags);

  sockaddr_in service;
  service.sin_family = AF_INET;
  service.sin_addr.s_addr = INADDR_ANY;
  service.sin_port = htons(50000);

  int a = bind(sock, (sockaddr *)&service, sizeof(sockaddr_in));

  return sock;
}

std::atomic<bool> server_running = false;

void runBenchmarkOn(int _sock) {
  struct pollfd fds[1];
  fds[0].fd = _sock;
  fds[0].events = POLLIN | POLLERR;

  int totalLat = 0;
  int count = 0;

  int offset = 0;
  char buff[1500] = {0};
  char pool[15000] = {0};

  auto start = std::chrono::system_clock::now();
  server_running = true;
  while (true) {
    int pollRC = poll(fds, 1, 100);
    if (pollRC > 0) {
      struct sockaddr_in src_addr;
      socklen_t len;
      int sze = recvfrom(_sock, buff, sizeof(buff), 0, (struct sockaddr*)&src_addr, &len);

      if (sze == -1 || sze == 0) {
        std::cout << "sze is " << sze << std::endl;
        break;
      }

      std::memcpy(pool + offset, buff, sze);
      offset += sze;

      auto now = std::chrono::system_clock::now();
      auto diff = now - start;
      auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(diff);
      static int counter = 0;

      for (int i = 0; i < offset - 4; ++i) {
        // Find special marker
        if (pool[i + 0] == (char)15 && pool[i + 1] == (char)16 &&
            pool[i + 2] == (char)15 && pool[i + 3] == (char)16) {
          // Parse timestamp
          decltype(now) timestamp =
              *(decltype(now) *)(pool + i - sizeof(decltype(now)));

          auto latency = now - timestamp;
          auto latms =
              std::chrono::duration_cast<std::chrono::milliseconds>(latency);
          std::cout << "(" << std::setw(5) << (ms.count()) << ") ["
                    << std::setw(3) << counter++ << "] Received packet with "
                    << latms.count() << "ms latency." << std::endl;

          totalLat += latms.count();
          count += 1;

          char temp[sizeof(pool)];
          std::memcpy(temp, pool + (i + 4), sizeof(pool) - (i + 4));
          std::memcpy(pool, temp, sizeof(pool));
          offset -= (i + 4);
          break;
        }
      }
    }
  }
  server_running = false;

  std::cout << "Average latency: " << (totalLat / (float)count) << "ms and missing " << (1000-count) << " packets."
            << std::endl;
}

void clientBenchmark() {
  int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);

  struct sockaddr_in service;
  service.sin_family = AF_INET;
  service.sin_addr.s_addr = inet_addr("127.0.0.1");
  service.sin_port = htons(50000);

  for (int i = 0; i < 1000; ++i) {
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
    char buff[1500] = {0};
    auto now = std::chrono::system_clock::now();
    std::memcpy(buff, &now, sizeof(now));

    buff[sizeof(now) + 0] = (char)15;
    buff[sizeof(now) + 1] = (char)16;
    buff[sizeof(now) + 2] = (char)15;
    buff[sizeof(now) + 3] = (char)16;
    sendto(sock, buff, sizeof(buff), 0, (const struct sockaddr *)&service,
           sizeof(service));
  }
  while (server_running) {
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    sendto(sock, nullptr, 0, 0, (const struct sockaddr *)&service,
           sizeof(service));
  }
  close(sock);
}
void run() {
  std::thread t([]() {
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    clientBenchmark();
  });

  int sock = serverWaitForConnection();
  if (sock) {
    runBenchmarkOn(sock);
  }

  t.join();
}
} // namespace UDP_test

// In order to emulate packet loss:
// Find the name of your NIC on linux:
//    Use `ip link show` to list all devices. Commonly the device you want is enth0.
// Then you can use `tc` to emulate all sorts of network conditions. We're specifically interested in packet loss:
//    sudo tc qdisc add dev <device_name> root netem loss 10%
// WARNING: Percents higher than 20 will tank an SSH connection.
// To restore your settings:
//    sudo tc qdisc del dev <device_name> root
// 

// My results:
// With TCP and 10% packet loss the results vary wildly. Here's 3 runs:
// Average latency: 236.782ms
// Average latency: 210.072ms
// Average latency: 4091.71ms
// While UDP can not only detect the packet loss, but doesn't skip a beat in it's latency:
// Average latency: 0ms and missing 116 packets.
// Average latency: 0ms and missing 114 packets.
// Average latency: 0ms and missing 92 packets.

int main(int argc, char **argv) {
  if (argc < 2) {
    std::cout << "Usage: ptest [udp|tcp] <for tcp, optional second arg 1 or 0 to enable or disable Nagle's algorithm>" << std::endl;
  }
  if (strcmp(argv[1], "udp") == 0) {
    UDP_test::run();
  } else {
    bool nagles = false;
    if (argc == 3) {
      nagles = (strcmp(argv[2], "1") == 0);
    }
    TCP_test::run(nagles);
  }
}