function(jot_collect_python_embed_flags out_compile_opts out_include_dirs out_link_opts)
  execute_process(
    COMMAND python3-config --cflags
    OUTPUT_VARIABLE _python_cflags
    OUTPUT_STRIP_TRAILING_WHITESPACE
  )

  string(REGEX MATCHALL "-I([^ ]+)" _python_include_flags "${_python_cflags}")
  set(_python_includes "")
  foreach(_inc_flag ${_python_include_flags})
    string(SUBSTRING "${_inc_flag}" 2 -1 _inc_dir)
    list(APPEND _python_includes "${_inc_dir}")
  endforeach()
  if(_python_includes)
    list(REMOVE_DUPLICATES _python_includes)
  endif()

  string(REGEX REPLACE "-I[^ ]+" "" _python_other_flags "${_python_cflags}")
  string(STRIP "${_python_other_flags}" _python_other_flags)
  separate_arguments(_python_other_flags_list UNIX_COMMAND "${_python_other_flags}")

  execute_process(
    COMMAND python3-config --ldflags --embed
    OUTPUT_VARIABLE _python_ldflags
    OUTPUT_STRIP_TRAILING_WHITESPACE
  )
  string(STRIP "${_python_ldflags}" _python_ldflags)
  separate_arguments(_python_ldflags_list UNIX_COMMAND "${_python_ldflags}")

  set(${out_compile_opts} ${_python_other_flags_list} PARENT_SCOPE)
  set(${out_include_dirs} ${_python_includes} PARENT_SCOPE)
  set(${out_link_opts} ${_python_ldflags_list} PARENT_SCOPE)
endfunction()
