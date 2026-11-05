# Shared compiler settings for every Ember target.
function(ember_configure_target target folder)
    target_compile_features(${target} PUBLIC cxx_std_20)

    if(MSVC)
        target_compile_options(${target} PRIVATE
            /W4                     # high warning level
            /permissive-            # standards conformance
            /utf-8
            /Zc:__cplusplus
            /Zc:preprocessor
            /external:anglebrackets # treat <third-party> headers as external...
            /external:W0            # ...and silence their warnings
            /MP                     # parallel compilation (Visual Studio generator)
        )
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)
    endif()

    target_compile_definitions(${target} PUBLIC
        $<$<CONFIG:Debug>:EMBER_DEBUG=1>
        $<$<PLATFORM_ID:Windows>:NOMINMAX>
        $<$<PLATFORM_ID:Windows>:WIN32_LEAN_AND_MEAN>
    )

    set_target_properties(${target} PROPERTIES FOLDER "${folder}")
endfunction()
