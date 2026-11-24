#include "capture/src/macos/MacCaptureDevice.h"

#import <AVFoundation/AVFoundation.h>
#import <dispatch/dispatch.h>

namespace CAPTURE {
namespace MAC {
namespace {

bool RequestAccessForMediaType(AVMediaType media_type) {
  const AVAuthorizationStatus status =
      [AVCaptureDevice authorizationStatusForMediaType:media_type];
  if (status == AVAuthorizationStatusAuthorized) {
    return true;
  }
  if (status == AVAuthorizationStatusDenied ||
      status == AVAuthorizationStatusRestricted) {
    return false;
  }

  __block BOOL granted = NO;
  dispatch_semaphore_t semaphore = dispatch_semaphore_create(0);
  [AVCaptureDevice requestAccessForMediaType:media_type
                           completionHandler:^(BOOL current_granted) {
                             granted = current_granted;
                             dispatch_semaphore_signal(semaphore);
                           }];
  dispatch_semaphore_wait(semaphore, DISPATCH_TIME_FOREVER);
  return granted;
}

void AppendDevice(NSMutableOrderedSet<NSString *> *seen_ids,
                  std::vector<std::pair<std::string, std::string>> *devices,
                  AVCaptureDevice *device) {
  if (device == nil || device.uniqueID.length == 0) {
    return;
  }

  if ([seen_ids containsObject:device.uniqueID]) {
    return;
  }

  [seen_ids addObject:device.uniqueID];
  devices->emplace_back(device.uniqueID.UTF8String,
                        device.localizedName.UTF8String);
}

} // namespace

bool RequestCameraAccess() {
  return RequestAccessForMediaType(AVMediaTypeVideo);
}

bool RequestMicrophoneAccess() {
  return RequestAccessForMediaType(AVMediaTypeAudio);
}

std::vector<std::pair<std::string, std::string>> GetCameraDevices() {
  std::vector<std::pair<std::string, std::string>> devices;
  if (!RequestCameraAccess()) {
    return devices;
  }

  NSMutableOrderedSet<NSString *> *seen_ids = [NSMutableOrderedSet orderedSet];

  NSArray<AVCaptureDeviceType> *device_types = nil;
  if (@available(macOS 13, *)) {
    device_types = @[
      AVCaptureDeviceTypeBuiltInWideAngleCamera,
      AVCaptureDeviceTypeExternalUnknown, AVCaptureDeviceTypeDeskViewCamera
    ];
  } else {
    device_types = @[
      AVCaptureDeviceTypeBuiltInWideAngleCamera,
      AVCaptureDeviceTypeExternalUnknown
    ];
  }

  AVCaptureDeviceDiscoverySession *video_session =
      [AVCaptureDeviceDiscoverySession
          discoverySessionWithDeviceTypes:device_types
                                mediaType:AVMediaTypeVideo
                                 position:AVCaptureDevicePositionUnspecified];
  for (AVCaptureDevice *device in video_session.devices) {
    AppendDevice(seen_ids, &devices, device);
  }

  AVCaptureDeviceDiscoverySession *muxed_session =
      [AVCaptureDeviceDiscoverySession
          discoverySessionWithDeviceTypes:device_types
                                mediaType:AVMediaTypeMuxed
                                 position:AVCaptureDevicePositionUnspecified];
  for (AVCaptureDevice *device in muxed_session.devices) {
    AppendDevice(seen_ids, &devices, device);
  }

  return devices;
}

std::vector<std::pair<std::string, std::string>> GetMicrophoneDevices() {
  std::vector<std::pair<std::string, std::string>> devices;
  if (!RequestMicrophoneAccess()) {
    return devices;
  }

  NSMutableOrderedSet<NSString *> *seen_ids = [NSMutableOrderedSet orderedSet];
  NSArray<AVCaptureDevice *> *audio_devices =
      [AVCaptureDevice devicesWithMediaType:AVMediaTypeAudio];
  for (AVCaptureDevice *device in audio_devices) {
    AppendDevice(seen_ids, &devices, device);
  }

  return devices;
}

} // namespace MAC
} // namespace CAPTURE
