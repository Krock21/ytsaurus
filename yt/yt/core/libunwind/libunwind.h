#pragma once

namespace NYT::NLibunwind {

////////////////////////////////////////////////////////////////////////////////
// Thin wrapper around libunwind.

int GetStackTrace(void** frames, int maxFrames, int skipFrames);

////////////////////////////////////////////////////////////////////////////////

} // namespace NYT::NLibunwind