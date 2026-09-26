// Non-Windows builds only need the engine (tests, dry runs); there is no
// software input injection here.
#include "backend.h"

std::unique_ptr<Backend> createSoftwareBackend() { return nullptr; }
