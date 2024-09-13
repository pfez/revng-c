#
# This file is distributed under the MIT License. See LICENSE.md for details.
#

revng_add_analyses_library(
  revngcCFGFlattening
  revngc
  CFGFlatteningPass.cpp)

target_link_libraries(revngcCFGFlattening revngcSupport revng::revngSupport ${LLVM_LIBRARIES})

