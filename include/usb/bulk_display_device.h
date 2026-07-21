// libusb wrapper for the KOMPLETE KONTROL MK2 LCD bulk-OUT endpoint
// (interface 3, vendor class 0xff, endpoint 0x03). This interface has no
// kernel driver attached, so it is only reachable via libusb, unlike the
// HID interface (see usb/hid_device.h, which uses hidraw instead).
#pragma once

#include <cstdint>
#include <string>
#include <vector>

struct libusb_context;
struct libusb_device_handle;

namespace mk2 {

class LcdBulkDevice {
 public:
  LcdBulkDevice() = default;
  ~LcdBulkDevice();

  LcdBulkDevice(const LcdBulkDevice&) = delete;
  LcdBulkDevice& operator=(const LcdBulkDevice&) = delete;

  // Initializes libusb, finds the first S49/S61/S88 MK2, and claims the
  // vendor-class bulk interface (descriptor-driven: scans all interfaces of
  // the matched device for a bulk OUT endpoint rather than hard-coding
  // interface 3 / endpoint 0x03, though those are the expected values).
  bool Open();
  void Close();
  bool IsOpen() const { return handle_ != nullptr; }

  // Writes a pre-built LCD packet (see display/lcd_packet.h) to the bulk OUT
  // endpoint. Returns true if the full packet was written within the
  // timeout.
  bool WritePacket(const std::vector<uint8_t>& packet, int timeout_ms = 5000);

  int interface_number() const { return interface_number_; }
  uint8_t endpoint_address() const { return endpoint_address_; }
  const std::string& last_error() const { return last_error_; }

 private:
  libusb_context* ctx_ = nullptr;
  libusb_device_handle* handle_ = nullptr;
  int interface_number_ = -1;
  uint8_t endpoint_address_ = 0;
  bool claimed_ = false;
  std::string last_error_;
};

}  // namespace mk2
