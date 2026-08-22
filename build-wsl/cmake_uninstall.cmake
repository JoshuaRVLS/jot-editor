if(NOT EXISTS "/mnt/c/Users/Joshua Ravael/Documents/jot/build-wsl/install_manifest.txt")
  message(FATAL_ERROR
    "Cannot find install manifest: /mnt/c/Users/Joshua Ravael/Documents/jot/build-wsl/install_manifest.txt. "
    "Run 'cmake --install .' first to generate it, or run a build that installs.")
endif()

file(READ "/mnt/c/Users/Joshua Ravael/Documents/jot/build-wsl/install_manifest.txt" files)
string(REGEX REPLACE "\n" ";" files "${files}")

foreach(file ${files})
  message(STATUS "Removing: ${file}")
  if(IS_SYMLINK "${file}" OR EXISTS "${file}")
    file(REMOVE "${file}")
  endif()
endforeach()

# Best-effort cleanup of directories we created and that are now empty.
set(_empty_dirs
  "/usr/local/share/jot/python/runtime"
  "/usr/local/share/jot/python"
  "/usr/local/share/jot/configs/colors"
  "/usr/local/share/jot/configs"
  "/usr/local/share/jot"
)
foreach(d ${_empty_dirs})
  if(IS_DIRECTORY "${d}")
    file(GLOB _children "${d}/*" "${d}/.*")
    list(FILTER _children EXCLUDE REGEX "/\\.+$")
    list(LENGTH _children _n)
    if(_n EQUAL 0)
      file(REMOVE_RECURSE "${d}")
    endif()
  endif()
endforeach()

message(STATUS "Uninstall complete.")
