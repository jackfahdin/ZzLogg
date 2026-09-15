#pragma once
#include "zzlogg/update/manifest.h"
namespace zzlogg::update::detail {
// Internal only: signature validation does not make a payload installation-ready.
struct EnvelopeResult {
    std::optional<std::string> payload;
    VerificationError error = VerificationError::EnvelopeInvalid;
};
EnvelopeResult verifyEnvelope(std::string_view, const VerificationContext&);
}
