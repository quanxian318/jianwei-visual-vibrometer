#pragma once

// Line commands on the USB CDC port for bench work without the AX7010:
//   help | status | events | sd | nfc | play <asset_id> [volume] | stop
//   wifi <ssid> [password] | wifi off | baud <rate> | flow on|off
//   log error|warn|info|debug | reboot
// The Wi-Fi password is never echoed.
namespace console {
void poll();
}
