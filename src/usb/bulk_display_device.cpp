#include "usb/bulk_display_device.h"

#include <libusb-1.0/libusb.h>

#include "mk2_protocol.h"

namespace mk2 {

LcdBulkDevice::~LcdBulkDevice() { Close(); }

bool LcdBulkDevice::Open() {
  if (libusb_init(&ctx_) != 0) {
    last_error_ = "libusb_init failed";
    return false;
  }

  libusb_device** list = nullptr;
  ssize_t count = libusb_get_device_list(ctx_, &list);
  if (count < 0) {
    last_error_ = "libusb_get_device_list failed";
    libusb_exit(ctx_);
    ctx_ = nullptr;
    return false;
  }

  for (ssize_t i = 0; i < count && handle_ == nullptr; ++i) {
    libusb_device* dev = list[i];
    libusb_device_descriptor desc{};
    if (libusb_get_device_descriptor(dev, &desc) != 0) continue;
    if (desc.idVendor != kNiVendorId) continue;

    bool supported = false;
    for (uint16_t pid : kSupportedProductIds) {
      if (desc.idProduct == pid) supported = true;
    }
    if (!supported) continue;

    libusb_config_descriptor* config = nullptr;
    if (libusb_get_active_config_descriptor(dev, &config) != 0) continue;

    int found_interface = -1;
    uint8_t found_endpoint = 0;
    for (int iface_idx = 0; iface_idx < config->bNumInterfaces; ++iface_idx) {
      const libusb_interface& iface = config->interface[iface_idx];
      for (int alt = 0; alt < iface.num_altsetting; ++alt) {
        const libusb_interface_descriptor& alt_desc = iface.altsetting[alt];
        for (int ep = 0; ep < alt_desc.bNumEndpoints; ++ep) {
          const libusb_endpoint_descriptor& ep_desc = alt_desc.endpoint[ep];
          bool is_bulk = (ep_desc.bmAttributes & LIBUSB_TRANSFER_TYPE_MASK) ==
                          LIBUSB_TRANSFER_TYPE_BULK;
          bool is_out = (ep_desc.bEndpointAddress & LIBUSB_ENDPOINT_DIR_MASK) ==
                        LIBUSB_ENDPOINT_OUT;
          if (is_bulk && is_out) {
            found_interface = alt_desc.bInterfaceNumber;
            found_endpoint = ep_desc.bEndpointAddress;
          }
        }
      }
      if (found_interface == kLcdBulkInterfaceNumber) break;  // prefer iface 3
    }
    libusb_free_config_descriptor(config);

    if (found_interface < 0) continue;

    libusb_device_handle* handle = nullptr;
    if (libusb_open(dev, &handle) != 0) continue;

    libusb_set_auto_detach_kernel_driver(handle, 1);
    if (libusb_claim_interface(handle, found_interface) != 0) {
      libusb_close(handle);
      continue;
    }

    handle_ = handle;
    interface_number_ = found_interface;
    endpoint_address_ = found_endpoint;
    claimed_ = true;
  }

  libusb_free_device_list(list, 1);

  if (handle_ == nullptr) {
    last_error_ = "no KOMPLETE KONTROL MK2 bulk OUT endpoint found";
    libusb_exit(ctx_);
    ctx_ = nullptr;
    return false;
  }
  return true;
}

void LcdBulkDevice::Close() {
  if (handle_ != nullptr) {
    if (claimed_) {
      libusb_release_interface(handle_, interface_number_);
      claimed_ = false;
    }
    libusb_close(handle_);
    handle_ = nullptr;
  }
  if (ctx_ != nullptr) {
    libusb_exit(ctx_);
    ctx_ = nullptr;
  }
}

bool LcdBulkDevice::WritePacket(const std::vector<uint8_t>& packet,
                                 int timeout_ms) {
  if (handle_ == nullptr || packet.empty()) return false;

  int transferred = 0;
  int rc = libusb_bulk_transfer(
      handle_, endpoint_address_, const_cast<uint8_t*>(packet.data()),
      static_cast<int>(packet.size()), &transferred, timeout_ms);
  if (rc != 0) {
    last_error_ = std::string("libusb_bulk_transfer failed: ") +
                  libusb_error_name(rc);
    return false;
  }
  return transferred == static_cast<int>(packet.size());
}

}  // namespace mk2
