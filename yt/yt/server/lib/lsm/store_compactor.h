#pragma once

#include "lsm_backend.h"

namespace NYT::NLsm {

////////////////////////////////////////////////////////////////////////////////

ILsmBackendPtr CreateStoreCompactor();

////////////////////////////////////////////////////////////////////////////////

} // namespace NYT::NLsm
