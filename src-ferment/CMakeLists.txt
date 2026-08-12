# Ferment Family — standalone per-plugin products.
#
# Each plugin lives in its own subdirectory and builds as an independent
# JUCE plugin (VST3 / AU / CLAP / Standalone).  They link the DSP sources
# from src/autogen_airwin/ and share no runtime code with the consolidated
# plugin in src-juce/.
#
# Reuse the JUCE + CLAP fetches already set up in src-juce/CMakeLists.txt
# (they're CPM-cached so adding again is a no-op).

# Shared vector UI kit (FermentKnob, NeedleMeter, EqGraphEditor, chassis).
# A JUCE-style module, so every plugin picks it up by linking one target.
juce_add_module(${CMAKE_CURRENT_SOURCE_DIR}/ui/ferment_ui ALIAS_NAMESPACE ferment)

# Format list local to this subdirectory — src-juce declares its own.
if (NOT DEFINED AWCO_ARM64EC)
  list(APPEND FERMENT_FORMATS VST3)
endif()
if (APPLE)
  list(APPEND FERMENT_FORMATS AU)
endif()
list(APPEND FERMENT_FORMATS Standalone)
if (UNIX AND NOT APPLE)
  list(APPEND FERMENT_FORMATS LV2)
endif()

# Helper: creates one Ferment plugin product.
# Arguments: TARGET_NAME PRODUCT_NAME BUNDLE_ID MFR_CODE PLUG_CODE LV2_URI CLAP_ID SRC_DIR
function(ferment_add_plugin TARGET_NAME PRODUCT_NAME BUNDLE_ID MFR_CODE PLUG_CODE LV2_URI CLAP_ID SRC_DIR)
  juce_add_plugin(${TARGET_NAME}
    PRODUCT_NAME "${PRODUCT_NAME}"
    COMPANY_NAME "Ferment"
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
    ferment-base
    ferment::ferment_ui
    juce::juce_audio_utils
    juce::juce_audio_processors
  )

  clap_juce_extensions_plugin(TARGET ${TARGET_NAME}
    CLAP_ID "${CLAP_ID}"
    CLAP_PROCESS_EVENTS_RESOLUTION_SAMPLES TRUE
    CLAP_FEATURES "audio-effect" "compressor" "stereo")
endfunction()

# Shared DSP primitives for all Ferment plugins.  The UI half of this used to
# be WarmLookAndFeel; it now lives in the ferment_ui module above.
set(FERMENT_COMMON_SOURCES
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
  glue/FermentGlue.cpp
  glue/FermentGlueProc.cpp
  glue/FermentGlue.h
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
  eq/FermentEq.cpp
  eq/FermentEq.h
  ${FERMENT_COMMON_SOURCES}
)

# ---- Ferment Charge ----
ferment_add_plugin(
  ferment-charge
  "Ferment Charge"
  "com.ferment.dsp.charge"
  Frmt
  FmCh
  "https://ferment.dsp/lv2/charge"
  "com.ferment.dsp.charge"
  charge
)
target_sources(ferment-charge PRIVATE
  charge/FermentChargeProcessor.cpp
  charge/FermentChargeProcessor.h
  charge/FermentChargeEditor.cpp
  charge/FermentChargeEditor.h
  charge/FermentCharge.cpp
  charge/FermentChargeProc.cpp
  charge/FermentCharge.h
  charge/FermentChargeTables.h
  ${FERMENT_COMMON_SOURCES}
)

# ---- Ferment Clip ----
ferment_add_plugin(
  ferment-clip
  "Ferment Clip"
  "com.ferment.dsp.clip"
  Frmt
  FmCp
  "https://ferment.dsp/lv2/clip"
  "com.ferment.dsp.clip"
  clip
)
target_sources(ferment-clip PRIVATE
  clip/FermentClipProcessor.cpp
  clip/FermentClipProcessor.h
  clip/FermentClipEditor.cpp
  clip/FermentClipEditor.h
  clip/FermentClip.cpp
  clip/FermentClipProc.cpp
  clip/FermentClip.h
  ${FERMENT_COMMON_SOURCES}
)

# ---- Ferment Limit ----
ferment_add_plugin(
  ferment-limit
  "Ferment Limit"
  "com.ferment.dsp.limit"
  Frmt
  FmLm
  "https://ferment.dsp/lv2/limit"
  "com.ferment.dsp.limit"
  limit
)
target_sources(ferment-limit PRIVATE
  limit/FermentLimitProcessor.cpp
  limit/FermentLimitProcessor.h
  limit/FermentLimitEditor.cpp
  limit/FermentLimitEditor.h
  limit/FermentLimit.cpp
  limit/FermentLimitProc.cpp
  limit/FermentLimit.h
  ${FERMENT_COMMON_SOURCES}
)

# ---- Ferment Utility ----
ferment_add_plugin(
  ferment-utility
  "Ferment Utility"
  "com.ferment.dsp.utility"
  Frmt
  FmUt
  "https://ferment.dsp/lv2/utility"
  "com.ferment.dsp.utility"
  utility
)
target_sources(ferment-utility PRIVATE
  utility/FermentUtilityProcessor.cpp
  utility/FermentUtilityProcessor.h
  utility/FermentUtilityEditor.cpp
  utility/FermentUtilityEditor.h
  utility/FermentUtility.cpp
  utility/FermentUtility.h
  ${FERMENT_COMMON_SOURCES}
)


# ---- Ferment Percept ----
# The ear before the chain: realtime measurement (BS.1770 loudness, true
# peak, crest, tilt, band share, mono fold), verdicts, and the targets a
# master should hit.  The wrapper lives in percept/; core/ is the pure C++
# layer shared with Master, JUCE-free so the same code runs headless.
ferment_add_plugin(
  ferment-percept
  "Ferment Percept"
  "com.ferment.dsp.percept"
  Frmt
  FmAn
  "https://ferment.dsp/lv2/percept"
  "com.ferment.dsp.percept"
  percept
)
target_sources(ferment-percept PRIVATE
  percept/FermentPerceptProcessor.cpp
  percept/FermentPerceptProcessor.h
  percept/FermentPerceptEditor.cpp
  percept/FermentPerceptEditor.h
  core/AnalyzerCore.cpp
  core/AnalyzerCore.h
  core/FermentPolicy.cpp
  core/FermentPolicy.h
  ${FERMENT_COMMON_SOURCES}
)

# ---- Ferment Master ----
# The whole chain as one plugin: Stage -> Charge -> Tone -> Clip -> Limit,
# per-module latency-matched bypass, and one Learn that sets the chain from
# ten seconds of the track.
ferment_add_plugin(
  ferment-master
  "Ferment Master"
  "com.ferment.dsp.master"
  Frmt
  FmMa
  "https://ferment.dsp/lv2/master"
  "com.ferment.dsp.master"
  master
)
target_sources(ferment-master PRIVATE
  master/FermentMasterProcessor.cpp
  master/FermentMasterProcessor.h
  master/FermentMasterEditor.cpp
  master/FermentMasterEditor.h
  core/MasterCore.cpp
  core/MasterCore.h
  core/AnalyzerCore.cpp
  core/FermentPolicy.cpp
  charge/FermentCharge.cpp
  charge/FermentChargeProc.cpp
  clip/FermentClip.cpp
  clip/FermentClipProc.cpp
  limit/FermentLimit.cpp
  limit/FermentLimitProc.cpp
  ${FERMENT_COMMON_SOURCES}
)

# ---- screenshots ----
# One command to regenerate every plugin's editor at 1x and 2x into
# screenshots/ at the repo root:
#
#     cmake --build <build-dir> --target ferment-screenshots
#
add_custom_target(ferment-screenshots
  COMMAND ferment_editor_shots "${CMAKE_SOURCE_DIR}/screenshots"
  DEPENDS ferment_editor_shots
  COMMENT "Rendering plugin editor screenshots into screenshots/"
  VERBATIM
)

# ---- UI gallery app ----
# Design-review harness: a real macOS/Windows app showing every editor with a
# plugin picker and a display-scale picker.  Opens no audio device.
juce_add_gui_app(ferment_ui_gallery
  PRODUCT_NAME "Ferment UI Gallery"
  COMPANY_NAME "Ferment"
  BUNDLE_ID "com.ferment.dsp.uigallery"
)

# ---- editor screenshots ----
# Instantiates each plugin's real editor and paints it to PNG at 1x and 2x.
#
# Every processor .cpp defines createPluginFilter(), so linking all six into one
# binary needs the symbol renamed per translation unit.  Shims do that without
# touching the plugin sources themselves.
set(FERMENT_SHOT_PROCESSORS "")
foreach(entry
    "charge=charge/FermentChargeProcessor.cpp"
    "percept=percept/FermentPerceptProcessor.cpp"
    "master=master/FermentMasterProcessor.cpp"
    "clip=clip/FermentClipProcessor.cpp"
    "eq=eq/FermentEqProcessor.cpp"
    "glue=glue/FermentGlueProcessor.cpp"
    "limit=limit/FermentLimitProcessor.cpp"
    "utility=utility/FermentUtilityProcessor.cpp")
  string(REPLACE "=" ";" shot_parts "${entry}")
  list(GET shot_parts 0 shot_name)
  list(GET shot_parts 1 shot_src)
  set(shot_shim "${CMAKE_CURRENT_BINARY_DIR}/shots_${shot_name}.cpp")
  file(WRITE "${shot_shim}"
    "// Generated by src-ferment/CMakeLists.txt -- do not edit.\n"
    "#define createPluginFilter createPluginFilter_${shot_name}\n"
    "#include \"${CMAKE_CURRENT_SOURCE_DIR}/${shot_src}\"\n")
  list(APPEND FERMENT_SHOT_PROCESSORS "${shot_shim}")
endforeach()

# Everything a tool needs to instantiate all six editors.  Both dev tools want
# exactly this list; keeping one copy is what stops the gallery quietly falling
# a plugin behind the screenshotter.
set(FERMENT_ALL_EDITOR_SOURCES
  ${FERMENT_SHOT_PROCESSORS}
  charge/FermentChargeEditor.cpp charge/FermentCharge.cpp charge/FermentChargeProc.cpp
  clip/FermentClipEditor.cpp clip/FermentClip.cpp clip/FermentClipProc.cpp
  eq/FermentEqEditor.cpp eq/FermentEq.cpp
  glue/FermentGlueEditor.cpp glue/FermentGlue.cpp glue/FermentGlueProc.cpp
  limit/FermentLimitEditor.cpp limit/FermentLimit.cpp limit/FermentLimitProc.cpp
  utility/FermentUtilityEditor.cpp utility/FermentUtility.cpp
  percept/FermentPerceptEditor.cpp core/AnalyzerCore.cpp core/FermentPolicy.cpp
  master/FermentMasterEditor.cpp core/MasterCore.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/../src/airwin_consolidated_base.cpp
  ${FERMENT_COMMON_SOURCES}
)

add_executable(ferment_editor_shots
  ${CMAKE_CURRENT_SOURCE_DIR}/../tools/FermentEditorShots.cpp
  ${FERMENT_ALL_EDITOR_SOURCES}
)
target_link_libraries(ferment_editor_shots PRIVATE
  ferment-base
  ferment::ferment_ui
  juce::juce_audio_utils
  juce::juce_audio_processors
)
target_compile_definitions(ferment_editor_shots PRIVATE
  _USE_MATH_DEFINES=1
  JUCE_STANDALONE_APPLICATION=1
  JUCE_MODAL_LOOPS_PERMITTED=1
  JUCE_WEB_BROWSER=0
  JUCE_USE_CURL=0
  JUCE_USE_CAMERA=disabled
)

target_sources(ferment_ui_gallery PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/../tools/FermentUiGallery.cpp
  ${FERMENT_ALL_EDITOR_SOURCES}
)
target_link_libraries(ferment_ui_gallery PRIVATE
  ferment-base
  ferment::ferment_ui
  juce::juce_audio_utils
  juce::juce_audio_processors
  juce::juce_gui_extra
)
target_compile_definitions(ferment_ui_gallery PRIVATE
  _USE_MATH_DEFINES=1
  JUCE_WEB_BROWSER=0
  JUCE_USE_CURL=0
  JUCE_USE_CAMERA=disabled
)

# The module's unity TU #includes the component .cpps; Makefile generators do
# not track that, which produced stale binaries twice during review (§9 of the
# UI report). Declare the dependency explicitly.
file(GLOB FERMENT_UI_UNITY_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/ui/ferment_ui/*.cpp)
list(REMOVE_ITEM FERMENT_UI_UNITY_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/ui/ferment_ui/ferment_ui.cpp)
set_source_files_properties(${CMAKE_CURRENT_SOURCE_DIR}/ui/ferment_ui/ferment_ui.cpp
  PROPERTIES OBJECT_DEPENDS "${FERMENT_UI_UNITY_SOURCES}")
