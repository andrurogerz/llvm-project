set(LLVM_OPTIONAL_SOURCES
  FileUtilities.cpp
  InterfaceSupport.cpp
  StorageUniquer.cpp
  Timing.cpp
  ToolUtilities.cpp
  TypeID.cpp
)

set(MLIRSupport_sources
  FileUtilities.cpp
  InterfaceSupport.cpp
  RawOstreamExtras.cpp
  StateStack.cpp
  StorageUniquer.cpp
  Timing.cpp
  ToolUtilities.cpp
  TypeID.cpp
)

add_mlir_library(MLIRSupport
  ${MLIRSupport_sources}

  ADDITIONAL_HEADER_DIRS
  ${MLIR_MAIN_INCLUDE_DIR}/mlir/Support

  LINK_LIBS PUBLIC
  ${LLVM_PTHREAD_LIB})

if (NOT LLVM_LINK_LLVM_DYLIB)
  add_library(MLIRSupport_tablegen ALIAS MLIRSupport)
else()
  # Build a version of the support library that does not link against
  # libLLVM-*.so, to be used by clang-tblgen. This is so clang-tblgen doesn't
  # link against libLLVMSupport twice (once statically and once via
  # libLLVM-*.so).
  add_llvm_library(MLIRSupport_tablegen
    BUILDTREE_ONLY STATIC DISABLE_LLVM_LINK_LLVM_DYLIB
    ${MLIRSupport_sources}
    ADDITIONAL_HEADER_DIRS
    ${MLIR_MAIN_INCLUDE_DIR}/mlir/Support)
endif()
