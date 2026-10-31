# FC_SANITIZER selects one sanitizer for the whole build: "", "address", or "thread".
# ASan and TSan cannot be combined, so each gets its own preset.
set(FC_SANITIZER "" CACHE STRING "Sanitizer to enable: address, thread, or empty")
set_property(CACHE FC_SANITIZER PROPERTY STRINGS "" address thread)

if(FC_SANITIZER STREQUAL "address")
  add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer -fno-sanitize-recover=all)
  add_link_options(-fsanitize=address,undefined)
elseif(FC_SANITIZER STREQUAL "thread")
  add_compile_options(-fsanitize=thread -fno-omit-frame-pointer)
  add_link_options(-fsanitize=thread)
elseif(NOT FC_SANITIZER STREQUAL "")
  message(FATAL_ERROR "Unknown FC_SANITIZER '${FC_SANITIZER}'")
endif()
