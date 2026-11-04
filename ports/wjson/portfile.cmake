# Overlay port that builds the repository it lives in:
#   vcpkg install wjson[tool] --overlay-ports=<repo>/ports
#
# To submit to the vcpkg registry, replace SOURCE_PATH below with:
#   vcpkg_from_github(OUT_SOURCE_PATH SOURCE_PATH
#       REPO imontene/win-json-parser
#       REF "v${VERSION}"
#       SHA512 <sha512 of the release archive>
#       HEAD_REF main)
get_filename_component(SOURCE_PATH "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)

vcpkg_check_features(OUT_FEATURE_OPTIONS FEATURE_OPTIONS
    FEATURES
        tool WJSON_BUILD_CLI
)

vcpkg_cmake_configure(
    SOURCE_PATH "${SOURCE_PATH}"
    OPTIONS
        ${FEATURE_OPTIONS}
        -DWJSON_BUILD_TESTS=OFF
        -DWJSON_BUILD_BENCHMARKS=OFF
        -DWJSON_INSTALL=ON
)
vcpkg_cmake_install()
vcpkg_copy_pdbs()
vcpkg_cmake_config_fixup(CONFIG_PATH lib/cmake/wjson)

if("tool" IN_LIST FEATURES)
    vcpkg_copy_tools(TOOL_NAMES wjson AUTO_CLEAN)
endif()

file(REMOVE_RECURSE "${CURRENT_PACKAGES_DIR}/debug/include")
file(INSTALL "${CMAKE_CURRENT_LIST_DIR}/usage" DESTINATION "${CURRENT_PACKAGES_DIR}/share/${PORT}")
vcpkg_install_copyright(FILE_LIST "${SOURCE_PATH}/LICENSE" "${SOURCE_PATH}/THIRD_PARTY_NOTICES.md")
file(REMOVE_RECURSE "${CURRENT_PACKAGES_DIR}/share/doc" "${CURRENT_PACKAGES_DIR}/debug/share/doc")
