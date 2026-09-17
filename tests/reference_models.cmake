# Acquire the unchanged vendor library used by the frozen corpus regression.
# Keep third-party model text out of the neospice source distribution; the
# vendor's library references its original disk README for licensing terms.
set(NEOSPICE_RFF70N06_LIBRARY
    "${CMAKE_CURRENT_BINARY_DIR}/reference_models/harprmos.lib"
    CACHE FILEPATH "Pinned harprmos.lib used by the RFF70N06 regression")
set(_rff_sha256 "61469f6ff311b0c8efb6e62b037293843806ecb1b23c5de71472983628858dae")
set(_rff_url "https://raw.githubusercontent.com/kicad-spice-library/KiCad-Spice-Library/a8688952bcaab19f567bc4db237b60bde03ef310/Models/uncategorized/spice_complete/harprmos.lib")
if(NOT EXISTS "${NEOSPICE_RFF70N06_LIBRARY}")
    get_filename_component(_rff_dir "${NEOSPICE_RFF70N06_LIBRARY}" DIRECTORY)
    file(MAKE_DIRECTORY "${_rff_dir}")
    file(DOWNLOAD "${_rff_url}" "${NEOSPICE_RFF70N06_LIBRARY}.download"
        STATUS _rff_download_status TLS_VERIFY ON TIMEOUT 60)
    list(GET _rff_download_status 0 _rff_download_code)
    if(NOT _rff_download_code EQUAL 0)
        file(REMOVE "${NEOSPICE_RFF70N06_LIBRARY}.download")
        message(FATAL_ERROR "Cannot acquire pinned RFF70N06 library: ${_rff_download_status}. Supply NEOSPICE_RFF70N06_LIBRARY for offline builds.")
    endif()
    file(SHA256 "${NEOSPICE_RFF70N06_LIBRARY}.download" _rff_download_sha256)
    if(NOT _rff_download_sha256 STREQUAL _rff_sha256)
        file(REMOVE "${NEOSPICE_RFF70N06_LIBRARY}.download")
        message(FATAL_ERROR "Downloaded RFF70N06 reference library hash mismatch")
    endif()
    file(RENAME "${NEOSPICE_RFF70N06_LIBRARY}.download" "${NEOSPICE_RFF70N06_LIBRARY}")
endif()
file(SHA256 "${NEOSPICE_RFF70N06_LIBRARY}" _rff_actual_sha256)
if(NOT _rff_actual_sha256 STREQUAL _rff_sha256)
    message(FATAL_ERROR "RFF70N06 reference library hash mismatch: ${NEOSPICE_RFF70N06_LIBRARY}")
endif()
configure_file(circuits/rff70n06_op.cir.in generated/rff70n06_op.cir @ONLY)
