// Linux hidraw wrapper for the KOMPLETE KONTROL MK2 HID interface (knobs,
// buttons, jog wheel, Light Guide, button LEDs, control/keyzone assignment).
//
// The MK2's HID interface is claimed by the kernel hidraw driver, so this
// talks to it via /dev/hidrawN rather than libusb (which would require
// detaching the kernel driver). The bulk-transfer LCD path is separate, see
// usb/bulk_display_device.h, since interface 3 (vendor class 0xff) has no
// kernel driver and is only reachable via libusb.
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace mk2 {

class HidDevice {
 public:
  HidDevice() = default;
  ~HidDevice();

  HidDevice(const HidDevice&) = delete;
  HidDevice& operator=(const HidDevice&) = delete;

  // Scans /dev/hidraw* for a device matching one of kSupportedProductIds at
  // kNiVendorId and opens the first match. Returns false if none found or
  // open failed (check errno-based message via LastError()).
  bool OpenFirstSupported();

  // Opens a specific hidraw node, e.g. "/dev/hidraw3".
  bool OpenPath(const std::string& path);

  void Close();
  bool IsOpen() const { return fd_ >= 0; }

  // Blocking write of a single HID output report. `report` must start with
  // the report ID byte (e.g. kHidReportButtonLed).
  bool WriteReport(const std::vector<uint8_t>& report);

  // Blocking read of a single HID input report with a timeout. Returns
  // std::nullopt on timeout or error; an empty-checked vector on success.
  // Reports are typically kHidReportInput (0x01) sized reports.
  std::optional<std::vector<uint8_t>> ReadReport(int timeout_ms,
                                                  size_t max_len = 91);

  uint16_t vendor_id() const { return vendor_id_; }
  uint16_t product_id() const { return product_id_; }
  const std::string& path() const { return path_; }
  const std::string& last_error() const { return last_error_; }

 private:
  int fd_ = -1;
  uint16_t vendor_id_ = 0;
  uint16_t product_id_ = 0;
  std::string path_;
  std::string last_error_;
};

}  // namespace mk2
