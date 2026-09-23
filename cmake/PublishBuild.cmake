# Publish only after linking succeeds; readers always see a complete manifest.
file(READ "${BINARY_DIR}/generated/build-info.pending.json" metadata)
foreach(destination IN ITEMS "${BINARY_DIR}/build-info.json" "${APP_DIR}/build-info.json")
  get_filename_component(parent "${destination}" DIRECTORY)
  file(MAKE_DIRECTORY "${parent}")
  file(WRITE "${destination}.tmp" "${metadata}")
  file(RENAME "${destination}.tmp" "${destination}")
endforeach()
