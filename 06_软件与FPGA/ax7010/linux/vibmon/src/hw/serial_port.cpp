// termios2 is used for arbitrary baud rates; <termios.h> must not be included
// in this file because it conflicts with <asm/termbits.h>.
#include "hw/serial_port.h"

#include <asm/termbits.h>
#include <fcntl.h>
#include <poll.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>

#include "util/clock.h"
#include "util/log.h"

namespace vm {

SerialPort::~SerialPort() { close(); }

bool SerialPort::open(const std::string& device, uint32_t baud, bool rtscts) {
  close();
  fd_ = ::open(device.c_str(), O_RDWR | O_NOCTTY | O_NONBLOCK | O_CLOEXEC);
  if (fd_ < 0) {
    LOGE("serial", "open %s failed: %s", device.c_str(), strerror(errno));
    return false;
  }
  struct termios2 tio;
  if (ioctl(fd_, TCGETS2, &tio) != 0) {
    LOGE("serial", "TCGETS2 %s failed: %s", device.c_str(), strerror(errno));
    close();
    return false;
  }
  tio.c_iflag = 0;
  tio.c_oflag = 0;
  tio.c_lflag = 0;
  tio.c_cflag = CS8 | CREAD | CLOCAL | BOTHER;
  if (rtscts) tio.c_cflag |= CRTSCTS;
  tio.c_ispeed = baud;
  tio.c_ospeed = baud;
  tio.c_cc[VMIN] = 0;
  tio.c_cc[VTIME] = 0;
  if (ioctl(fd_, TCSETS2, &tio) != 0) {
    LOGE("serial", "TCSETS2 %s (%u baud) failed: %s", device.c_str(), baud, strerror(errno));
    close();
    return false;
  }
  device_ = device;
  flushInput();
  LOGI("serial", "%s %u 8N1%s", device.c_str(), baud, rtscts ? " RTS/CTS" : "");
  return true;
}

void SerialPort::close() {
  if (fd_ >= 0) ::close(fd_);
  fd_ = -1;
}

ssize_t SerialPort::readSome(uint8_t* buf, size_t n) {
  if (fd_ < 0) return -1;
  ssize_t r = ::read(fd_, buf, n);
  if (r < 0) return (errno == EAGAIN || errno == EINTR) ? 0 : -1;
  return r;
}

ssize_t SerialPort::writeSome(const uint8_t* buf, size_t n) {
  if (fd_ < 0) return -1;
  ssize_t w = ::write(fd_, buf, n);
  if (w < 0) return (errno == EAGAIN || errno == EINTR) ? 0 : -1;
  return w;
}

bool SerialPort::writeAll(const uint8_t* buf, size_t n, int timeoutMs, const std::atomic<bool>* cancel) {
  size_t off = 0;
  int64_t lastProgress = monoMs();
  while (off < n) {
    if (cancel && cancel->load()) return false;
    ssize_t w = writeSome(buf + off, n - off);
    if (w < 0) return false;
    if (w > 0) {
      off += static_cast<size_t>(w);
      lastProgress = monoMs();
      continue;
    }
    if (monoMs() - lastProgress > timeoutMs) return false;
    pollfd p{fd_, POLLOUT, 0};
    poll(&p, 1, 20);
  }
  return true;
}

bool SerialPort::waitReadable(int timeoutMs) {
  if (fd_ < 0) return false;
  pollfd p{fd_, POLLIN, 0};
  return poll(&p, 1, timeoutMs) > 0 && (p.revents & POLLIN);
}

void SerialPort::flushInput() {
  if (fd_ >= 0) ioctl(fd_, TCFLSH, TCIFLUSH);
}

void SerialPort::flushOutput() {
  if (fd_ >= 0) ioctl(fd_, TCFLSH, TCOFLUSH);
}

int SerialPort::outputQueued() const {
  int n = 0;
  if (fd_ < 0 || ioctl(fd_, TIOCOUTQ, &n) != 0) return 0;
  return n;
}

bool SerialPort::ctsAsserted() const {
  int bits = 0;
  if (fd_ < 0 || ioctl(fd_, TIOCMGET, &bits) != 0) return false;
  return (bits & TIOCM_CTS) != 0;
}

}  // namespace vm
