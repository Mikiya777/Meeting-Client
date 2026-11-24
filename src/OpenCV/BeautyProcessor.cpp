#include "OpenCV-美颜/BeautyProcessor.h"

#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>

#include <cstdio>
#include <exception>

namespace VCE {

std::shared_ptr<I420Frame> BeautyProcessor::Process(
    const std::shared_ptr<I420Frame> &frame) const {
  if (!IsEnabled() || !frame || !frame->HasData()) {
    return frame;
  }

  try {
    // Engine frames have tightly packed planes (no stride field).
    // Smooth Y directly to avoid RGB round trips and preserve chroma/range.
    // Borrow Y for this call. Both operations read source and write separate buffers.
    const cv::Mat source(frame->height, frame->width, CV_8UC1,
                         frame->data[0].get());
    cv::Mat smooth;
    cv::bilateralFilter(source, smooth, 5, 18.0, 3.0);

    auto output = std::make_shared<I420Frame>(*frame);
    output->data[0] = std::shared_ptr<uint8_t[]>(
        new uint8_t[frame->GetYPlaneSize()]);
    cv::Mat destination(frame->height, frame->width, CV_8UC1,
                        output->data[0].get());
    // Keep 65% original detail; no whitening or geometric changes.
    cv::addWeighted(source, 0.65, smooth, 0.35, 0.0, destination);
    // U/V remain shared read-only; dimensions, identity and timestamp survive.
    return output;
  } catch (const std::exception &error) {
    std::fprintf(stderr, "[Beauty] processing failed; using original frame: %s\n",
                 error.what());
    return frame;
  }
}

} // namespace VCE
