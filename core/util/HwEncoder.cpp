#include "core/util/HwEncoder.h"

extern "C" {
#include <libavcodec/avcodec.h>
}

namespace hwenc {

const AVCodec* findBestH264Encoder(QString* outName)
{
    // Order: vendor-specific → generic Windows HW → null.
    // h264_mf is kept as a last HW resort because its quality/performance is
    // usually worse than the vendor-specific path, but it works whenever a
    // vendor driver is installed even if the vendor-specific SDK runtime
    // isn't present.
    static const char* kCandidates[] = {
        "h264_nvenc",
        "h264_amf",
        "h264_qsv",
        "h264_mf",
    };

    for (const char* name : kCandidates) {
        const AVCodec* c = avcodec_find_encoder_by_name(name);
        if (c) {
            if (outName) *outName = QString::fromLatin1(name);
            return c;
        }
    }
    if (outName) outName->clear();
    return nullptr;
}

} // namespace hwenc
