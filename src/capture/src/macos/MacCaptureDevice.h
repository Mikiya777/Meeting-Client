#ifndef MAC_CAPTURE_DEVICE_H
#define MAC_CAPTURE_DEVICE_H

#include <string>
#include <utility>
#include <vector>

namespace CAPTURE {
namespace MAC {

std::vector<std::pair<std::string, std::string>> GetCameraDevices();
std::vector<std::pair<std::string, std::string>> GetMicrophoneDevices();

bool RequestCameraAccess();
bool RequestMicrophoneAccess();

} // namespace MAC
} // namespace CAPTURE

#endif // MAC_CAPTURE_DEVICE_H
