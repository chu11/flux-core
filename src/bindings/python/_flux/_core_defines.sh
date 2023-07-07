#!/bin/sh

# Special case #defines we want build into core bindings

grep "#define FLUX_CORE_VERSION_MAJOR" _core_clean.h
grep "#define FLUX_CORE_VERSION_MINOR" _core_clean.h
grep "#define FLUX_CORE_VERSION_PATCH" _core_clean.h
