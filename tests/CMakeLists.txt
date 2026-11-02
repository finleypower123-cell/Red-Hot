juce_add_console_app(PepperDspTests PRODUCT_NAME "PepperDspTests")
target_sources(PepperDspTests PRIVATE PepperDspTests.cpp ../Source/dsp/PepperEngine.cpp)
target_include_directories(PepperDspTests PRIVATE ../Source)
target_compile_definitions(PepperDspTests PRIVATE JUCE_USE_CURL=0 JUCE_WEB_BROWSER=0)
target_link_libraries(PepperDspTests PRIVATE
    juce::juce_core juce::juce_audio_basics juce::juce_audio_formats juce::juce_dsp
    juce::juce_recommended_config_flags)

# Renders the editor to a PNG and exercises the full plug-in processor (presets, state).
juce_add_console_app(PepperPluginTests PRODUCT_NAME "PepperPluginTests")
target_sources(PepperPluginTests PRIVATE PepperPluginTests.cpp
    ../Source/PluginProcessor.cpp ../Source/PluginEditor.cpp ../Source/Presets.cpp ../Source/dsp/PepperEngine.cpp
    ../Source/gui/PepperLookAndFeel.cpp ../Source/gui/PepperSelector.cpp)
target_include_directories(PepperPluginTests PRIVATE ../Source)
target_compile_definitions(PepperPluginTests PRIVATE JUCE_USE_CURL=0 JUCE_WEB_BROWSER=0)
target_link_libraries(PepperPluginTests PRIVATE juce::juce_audio_utils juce::juce_dsp juce::juce_recommended_config_flags)
