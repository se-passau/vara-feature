macro(add_vara_executable name)
  if (VARA_FEATURE_IN_TREE)
    set(LLVM_RUNTIME_OUTPUT_INTDIR ${CMAKE_BINARY_DIR}/${CMAKE_CFG_INTDIR}/bin)
    set(LLVM_LIBRARY_OUTPUT_INTDIR ${CMAKE_BINARY_DIR}/${CMAKE_CFG_INTDIR}/lib)
    add_llvm_executable( ${name} ${ARGN} )
    set_target_properties(${name} PROPERTIES FOLDER "vara-feature executables")
    install (TARGETS ${name}
      RUNTIME DESTINATION bin
    )
  else()
    add_executable( ${name} ${ARGN})
    install (TARGETS ${name}
      RUNTIME DESTINATION bin
    )
  endif()
endmacro(add_vara_executable)

macro(add_vara_library name)
  cmake_parse_arguments(ARG "" "" "" ${ARGN})
  set(srcs ${ARG_UNPARSED_ARGUMENTS})
  if(MSVC_IDE OR XCODE)
    file( GLOB_RECURSE headers *.h *.td *.def)
    set(srcs ${srcs} ${headers})
    string( REGEX MATCHALL "/[^/]+" split_path ${CMAKE_CURRENT_SOURCE_DIR})
    list( GET split_path -1 dir)
    file( GLOB_RECURSE headers
      ../../include/vara${dir}/*.h)
    set(srcs ${srcs} ${headers})
  endif(MSVC_IDE OR XCODE)
  if (MODULE)
    set(libkind MODULE)
  elseif (SHARED_LIBRARY)
    set(libkind SHARED)
  else()
    set(libkind)
  endif()
  add_library( ${name} ${libkind} ${srcs} )
  if( LLVM_COMMON_DEPENDS )
    add_dependencies( ${name} ${LLVM_COMMON_DEPENDS} )
  endif( LLVM_COMMON_DEPENDS )
  if( LLVM_USED_LIBS )
    foreach(lib ${LLVM_USED_LIBS})
      target_link_libraries( ${name} PUBLIC ${lib} )
    endforeach(lib)
  endif( LLVM_USED_LIBS )

  if(VARA_LINK_LIBS)
    foreach(lib ${VARA_LINK_LIBS})
      target_link_libraries(${name} PUBLIC ${lib})
    endforeach(lib)
  endif(VARA_LINK_LIBS)

  if( LLVM_LINK_COMPONENTS )
    if (USE_SHARED)
      llvm_config(${name} USE_SHARED ${LLVM_LINK_COMPONENTS})
    else()
      llvm_config(${name} ${LLVM_LINK_COMPONENTS})
    endif()
  endif( LLVM_LINK_COMPONENTS )
  if(MSVC)
    get_target_property(cflag ${name} COMPILE_FLAGS)
    if(NOT cflag)
      set(cflag "")
    endif(NOT cflag)
    set(cflag "${cflag} /Za")
    set_target_properties(${name} PROPERTIES COMPILE_FLAGS ${cflag})
  endif(MSVC)
  install(TARGETS ${name}
    EXPORT LLVMExports
    LIBRARY DESTINATION lib
    ARCHIVE DESTINATION lib${LLVM_LIBDIR_SUFFIX})
  set_property(GLOBAL APPEND PROPERTY LLVM_EXPORTS ${name})
endmacro(add_vara_library)

function(check_std_filesystems varname)
  set(OLD_CMAKE_REQUIRED_FLAGS ${CMAKE_REQUIRED_FLAGS})
  set(CMAKE_REQUIRED_FLAGS "-std=c++17 ${CMAKE_REQUIRED_FLAGS}")
  CHECK_CXX_SOURCE_COMPILES("
#include <filesystem>
namespace fs = std::filesystem;
int main() { return 0; }
" ${varname})
  set(CMAKE_REQUIRED_FLAGS ${OLD_CMAKE_REQUIRED_FLAGS})
endfunction()
