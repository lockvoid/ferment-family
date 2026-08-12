/*******************************************************************************
 BEGIN_JUCE_MODULE_DECLARATION

  ID:                 ferment_ui
  vendor:             Ferment DSP
  version:            1.0.0
  name:               Ferment UI
  description:        Vector component kit shared by the Ferment plugin family.
  website:            https://ferment.dsp/
  license:            Proprietary
  minimumCppStandard: 17

  dependencies:       juce_gui_basics, juce_audio_processors, juce_dsp

 END_JUCE_MODULE_DECLARATION
*******************************************************************************/

#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>

#include "FermentTheme.h"

#include "ChassisPanel.h"
#include "HeaderBar.h"
#include "FermentKnob.h"
#include "NeedleMeter.h"
#include "EqGraphEditor.h"
