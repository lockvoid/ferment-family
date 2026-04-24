# Ferment Family — standalone per-plugin products.
#
# Each plugin lives in its own subdirectory and builds as an independent
# JUCE plugin (VST3 / AU / CLAP / Standalone).  They link the DSP sources
# from src/autogen_airwin/ and share no runtime code with the consolidated
# plugin in src-juce/.
#
# Reuse the JUCE + CLAP fetches already set up in src-juce/CMakeLists.txt
# (they're CPM-cached so adding again is a no-op).

# Format list local to this subdirectory — src-juce declares its own.
if (NOT DEFINED AWCO_ARM64EC)
  list(APPEND FERMENT_FORMATS VST3)
endif()
list(APPEND FERMENT_FORMATS AU)
list(APPEND FERMENT_FORMATS Standalone)
if (UNIX AND NOT APPLE)
  list(APPEND FERMENT_FORMATS LV2)
endif()

# Helper: creates one Ferment plugin product.
# Arguments: TARGET_NAME PRODUCT_NAME BUNDLE_ID MFR_CODE PLUG_CODE LV2_URI CLAP_ID SRC_DIR
function(ferment_add_plugin TARGET_NAME PRODUCT_NAME BUNDLE_ID MFR_CODE PLUG_CODE LV2_URI CLAP_ID SRC_DIR)
  juce_add_plugin(${TARGET_NAME}
    PRODUCT_NAME "${PRODUCT_NAME}"
    COMPANY_NAME "Ferment DSP"
    COMPANY_WEBSITE "https://ferment.dsp/"
    BUNDLE_ID "${BUNDLE_ID}"
    PLUGIN_MANUFACTURER_CODE ${MFR_CODE}
    PLUGIN_CODE ${PLUG_CODE}

    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE

    COPY_PLUGIN_AFTER_BUILD ${COPY_PLUGIN_AFTER_BUILD}

    VST3_CATEGORIES Fx
    AU_MAIN_TYPE kAudioUnitType_Effect
    AU_SANDBOX_SAFE TR-UE

    LV2_URI ${LV2_URI}

    FORMATS ${FERMENT_FORMATS}
  )

  target_compile_definitions(${TARGET_NAME} PUBLIC
    JUCE_ALLOW_STATIC_NULL_VARIABLES=0
    JUCE_STRICT_REFCOUNTEDPOINTER=1
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_USE_CURL=0
    JUCE_WEB_BROWSER=0
    JUCE_USE_CAMERA=disabled
    JUCE_DISPLAY_SPLASH_SCREEN=0
    JUCE_REPORT_APP_USAGE=0
    JUCE_MODAL_LOOPS_PERMITTED=0
    JUCE_COREGRAPHICS_DRAW_ASYNC=1
    JUCE_ALSA=1 JUCE_JACK=1 JUCE_WASAPI=1 JUCE_DIRECTSOUND=1
    JUCE_CATCH_UNHANDLED_EXCEPTIONS=0
    _USE_MATH_DEFINES=1
    BUILD_HASH="${BUILD_HASH}"
  )

  target_link_libraries(${TARGET_NAME} PRIVATE
    airwin-registry
    juce::juce_audio_utils
    juce::juce_audio_processors
  )

  clap_juce_extensions_plugin(TARGET ${TARGET_NAME}
    CLAP_ID "${CLAP_ID}"
    CLAP_PROCESS_EVENTS_RESOLUTION_SAMPLES TRUE
    CLAP_FEATURES "audio-effect" "compressor" "stereo")
endfunction()

# Shared UI + DSP primitives for all Ferment plugins.
set(FERMENT_COMMON_SOURCES
  common/WarmLookAndFeel.cpp
  common/WarmLookAndFeel.h
  common/WarmPalette.h
  common/Biquad.h
)

# ---- Ferment Glue ----
ferment_add_plugin(
  ferment-glue
  "Ferment Glue"
  "com.ferment.dsp.glue"
  Frmt
  FmGl
  "https://ferment.dsp/lv2/glue"
  "com.ferment.dsp.glue"
  glue
)
target_sources(ferment-glue PRIVATE
  glue/FermentGlueProcessor.cpp
  glue/FermentGlueProcessor.h
  glue/FermentGlueEditor.cpp
  glue/FermentGlueEditor.h
  ${FERMENT_COMMON_SOURCES}
)

# ---- Ferment EQ ----
ferment_add_plugin(
  ferment-eq
  "Ferment EQ"
  "com.ferment.dsp.eq"
  Frmt
  FmEq
  "https://ferment.dsp/lv2/eq"
  "com.ferment.dsp.eq"
  eq
)
target_sources(ferment-eq PRIVATE
  eq/FermentEqProcessor.cpp
  eq/FermentEqProcessor.h
  eq/FermentEqEditor.cpp
  eq/FermentEqEditor.h
  ${FERMENT_COMMON_SOURCES}
)
