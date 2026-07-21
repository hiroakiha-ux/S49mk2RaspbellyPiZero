#include "usb/hid_device.h"

#include <dirent.h>
#include <fcntl.h>
#include <linux/hidraw.h>
#include <poll.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>

#include "mk2_protocol.h"

namespace mk2 {

namespace {

bool IsSupportedProductId(uint16_t pid) {
  for (uint16_t supported : kSupportedProductIds) {
    if (supported == pid) return true;
  }
  return false;
}

}  // namespace

HidDevice::~HidDevice() { Close(); }

bool HidDevice::OpenFirstSupported() {
  DIR* dir = opendir("/dev");
  if (dir == nullptr) {
    last_error_ = "opendir(/dev) failed: " + std::string(strerror(errno));
    return false;
  }

  bool opened = false;
  struct dirent* entry;
  while ((entry = readdir(dir)) != nullptr) {
    std::string name(entry->d_name);
    if (name.rfind("hidraw", 0) != 0) continue;

    std::string candidate = "/dev/" + name;
    int fd = ::open(candidate.c_str(), O_RDWR | O_NONBLOCK);
    if (fd < 0) continue;

    hidraw_devinfo info{};
    if (ioctl(fd, HIDIOCGRAWINFO, &info) < 0) {
      ::close(fd);
      continue;
    }

    uint16_t vendor = static_cast<uint16_t>(info.vendor);
    uint16_t product = static_cast<uint16_t>(info.product);
    if (vendor == kNiVendorId && IsSupportedProductId(product)) {
      fd_ = fd;
      vendor_id_ = vendor;
      product_id_ = product;
      path_ = candidate;
      opened = true;
      break;
    }
    ::close(fd);
  }
  closedir(dir);

  if (!opened) {
    last_error_ = "no matching hidraw device found for NI vendor 0x17cc";
  }
  return opened;
}

bool HidDevice::OpenPath(const std::string& path) {
  int fd = ::open(path.c_str(), O_RDWR | O_NONBLOCK);
  if (fd < 0) {
    last_error_ = "open(" + path + ") failed: " + std::string(strerror(errno));
    return false;
  }

  hidraw_devinfo info{};
  if (ioctl(fd, HIDIOCGRAWINFO, &info) == 0) {
    vendor_id_ = static_cast<uint16_t>(info.vendor);
    product_id_ = static_cast<uint16_t>(info.product);
  }

  fd_ = fd;
  path_ = path;
  return true;
}

void HidDevice::Close() {
  if (fd_ >= 0) {
    ::close(fd_);
    fd_ = -1;
  }
}

bool HidDevice::WriteReport(const std::vector<uint8_t>& report) {
  if (fd_ < 0 || report.empty()) return false;
  ssize_t written = ::write(fd_, report.data(), report.size());
  if (written < 0) {
    last_error_ = "write failed: " + std::string(strerror(errno));
    return false;
  }
  return static_cast<size_t>(written) == report.size();
}

std::optional<std::vector<uint8_t>> HidDevice::ReadReport(int timeout_ms,
                                                           size_t max_len) {
  if (fd_ < 0) return std::nullopt;

  pollfd pfd{};
  pfd.fd = fd_;
  pfd.events = POLLIN;
  int ready = poll(&pfd, 1, timeout_ms);
  if (ready <= 0) return std::nullopt;

  std::vector<uint8_t> buffer(max_len);
  ssize_t n = ::read(fd_, buffer.data(), buffer.size());
  if (n <= 0) return std::nullopt;
  buffer.resize(static_cast<size_t>(n));
  return buffer;
}

}  // namespace mk2
