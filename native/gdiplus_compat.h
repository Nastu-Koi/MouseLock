#pragma once
#include "theme.h"
#include <algorithm>

// The Windows SDK's GDI+ headers call the min/max macros that NOMINMAX removes.
namespace Gdiplus { using std::min; using std::max; }
#include <gdiplus.h>
