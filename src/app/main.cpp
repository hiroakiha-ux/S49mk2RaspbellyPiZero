// Entry point for the S49 MK2 <-> SEQTRAK Raspberry Pi Zero 2 W bridge.
//
// Usage: s49mk2_bridge [--dry-run] [--help]
//
//   --dry-run, -n   Open and read from all devices as normal, but never
//                   write to them: LCD packets and MIDI messages are hex
//                   dumped / summarized to stderr instead of sent. Use this
//                   for the first hardware bring-up on a Pi (see
//                   README.md's phased testing guide) before running live.
//
// Expects the Pi to be USB-hosting a hub with the KOMPLETE KONTROL S49 MK2
// and the YAMAHA SEQTRAK both attached, each exposing an ALSA rawmidi
// device (visible under /dev/snd/midiC*D*). See README.md for setup notes.
#include <csignal>
#include <cstdio>
#include <cstring>
#include <memory>

#include "app/controller_app.h"

namespace {

std::unique_ptr<mk2app::ControllerApp>* g_app_for_signal = nullptr;

void HandleSignal(int) {
  if (g_app_for_signal != nullptr && *g_app_for_signal != nullptr) {
    (*g_app_for_signal)->Stop();
  }
}

void PrintUsage(const char* argv0) {
  std::fprintf(stderr,
               "usage: %s [--dry-run|-n] [--help|-h]\n\n"
               "  --dry-run, -n   Read all devices as normal, but never "
               "write to them;\n"
               "                  LCD/MIDI output is hex-dumped to stderr "
               "instead.\n"
               "  --help, -h      Show this message and exit.\n",
               argv0);
}

}  // namespace

int main(int argc, char** argv) {
  bool dry_run = false;
  for (int i = 1; i < argc; ++i) {
    if (std::strcmp(argv[i], "--dry-run") == 0 ||
        std::strcmp(argv[i], "-n") == 0) {
      dry_run = true;
    } else if (std::strcmp(argv[i], "--help") == 0 ||
               std::strcmp(argv[i], "-h") == 0) {
      PrintUsage(argv[0]);
      return 0;
    } else {
      std::fprintf(stderr, "s49mk2_bridge: unknown argument: %s\n\n", argv[i]);
      PrintUsage(argv[0]);
      return 2;
    }
  }

  auto app = std::make_unique<mk2app::ControllerApp>();
  g_app_for_signal = &app;
  std::signal(SIGINT, HandleSignal);
  std::signal(SIGTERM, HandleSignal);

  app->SetDryRun(dry_run);

  if (!app->Initialize()) {
    std::fprintf(stderr, "s49mk2_bridge: initialization failed\n");
    return 1;
  }

  std::fprintf(stderr, "s49mk2_bridge: running%s (Ctrl-C to stop)\n",
               dry_run ? " in --dry-run mode" : "");
  app->Run();
  std::fprintf(stderr, "s49mk2_bridge: stopped\n");
  return 0;
}
