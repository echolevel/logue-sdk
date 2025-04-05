#pragma once
/*
    BSD 3-Clause License

    Copyright (c) 2023, KORG INC.
    All rights reserved.

    Redistribution and use in source and binary forms, with or without
    modification, are permitted provided that the following conditions are met:

    * Redistributions of source code must retain the above copyright notice, this
      list of conditions and the following disclaimer.

    * Redistributions in binary form must reproduce the above copyright notice,
      this list of conditions and the following disclaimer in the documentation
      and/or other materials provided with the distribution.

    * Neither the name of the copyright holder nor the names of its
      contributors may be used to endorse or promote products derived from
      this software without specific prior written permission.

    THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
    AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
    IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
    DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
    FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
    DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
    SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
    CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
    OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
    OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

//*/

/*
 *  File: effect.h
 *
 *  echolevel-loopitch
 * 
 *  Pitch adjustable looper effect for Korg NTS-3 Kaoss Pad
 * 
*   https://github.com/echolevel/logue-sdk/tree/master/platform/nts-3_kaoss/echolevel-loopitch
 *
 */

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <climits>

#include "unit_genericfx.h"   // Note: Include base definitions for genericfx units

#include "utils/buffer_ops.h" // for buf_clr_f32()
#include "utils/int_math.h"   // for clipminmaxi32()

// === Defines ===
#define MIN(a, b) ((a) < (b) ? (a) : (b))
#define MAX(a, b) ((a) > (b) ? (a) : (b))
#define CLAMP(val, minval, maxval) (MAX((minval), MIN((val), (maxval))))

// WARNING - never use fewer than 2 grains (div zero risk)
#define MAX_GRAINS 8

// === Globals (static) ===
static uint32_t bufferWritePos = 0;
static float bufferReadPosL = 0.0f;
static float bufferReadPosR = 0.0f;
static uint32_t bufferLength = 0;
static uint32_t bufferLengthL = 0;
static uint32_t bufferLengthR = 0;

static int isRecording = 0;
static int isPlaying = 0;
static bool shouldRandomise = false;
static bool touchEngaged = false;
static bool grainModeEnabled = true;

uint32_t rng_state = 123456789; // Seed — can be anything

uint32_t fast_rand_u32() {
  rng_state = rng_state * 1664525 + 1013904223; // LCG constants
  return rng_state;
}

uint32_t fast_rand_u32_range(uint32_t min, uint32_t max) {
  uint32_t r = fast_rand_u32();
  uint32_t range = max - min + 1;
  return min + (r % range);
}

float fast_randf(float min_val, float max_val) {
  return min_val + (fast_rand_u32() / 4294967296.0f) * (max_val - min_val);
}

class Effect {
 public:
  /*===========================================================================*/
  /* Public Data Structures/Types/Enums. */
  /*===========================================================================*/


  enum {
    BUFFER_LENGTH = 0x80000 
  };

  enum {
    PARAM1 = 0U,
    PARAM2,
    DEPTH,
    PITCHMODE, 
    PLAYMODE, 
    DRIFT,
    NUM_PARAMS
  };

  // Note: Make sure that default param values correspond to declarations in header.c
  struct Params {
    float param1{0.f};
    float param2{0.f};
    float depth{0.f};
    uint32_t param4{1};
    uint32_t param5{1};
    float param6{0.f};

    void reset() 
    {
      param1 = 0.f;
      param2 = 0.f;
      depth = 0.f;
      param4 = 0;
      param5 = 0;
      param6 = 0.f;
    }
  };

  // PITCHMODE
  enum {
    PARAM4_VALUE0 = 0, // unquantised hz
    PARAM4_VALUE1, // 7 semitones up/down
    PARAM4_VALUE2, // 12 semitones up/down
    PARAM4_VALUE3, // 24 semitones up/down
    NUM_PARAM4_VALUES,
  };

  // PLAYMODE
  enum {
    PARAM5_VALUE0 = 0, // auto play
    PARAM5_VALUE1, // touch play
    NUM_PARAM5_VALUES,
  };
  
  typedef struct {
    float readPos;
    float speed;
    float loopLength;
    float startOffset;
    float pan; // -1.0 (left) to 1.0 (right)
  } Grain;

  /*===========================================================================*/
  /* Lifecycle Methods. */
  /*===========================================================================*/

  Effect(void) {}
  ~Effect(void) {} // Note: will never actually be called for statically allocated instances

  inline int8_t Init(const unit_runtime_desc_t * desc) {
    if (!desc)
      return k_unit_err_undef;
    
    // Note: make sure the unit is being loaded to the correct platform/module target
    if (desc->target != unit_header.common.target)
      return k_unit_err_target;
    
    // Note: check API compatibility with the one this unit was built against
    if (!UNIT_API_IS_COMPAT(desc->api))
      return k_unit_err_api_version;
    
    // Check compatibility of samplerate with unit, for NTS-3 kaoss pad kit should be 48000
    if (desc->samplerate != 48000)
      return k_unit_err_samplerate;

    // Check compatibility of frame geometry
    if (desc->input_channels != 2 || desc->output_channels != 2)  // should be stereo input/output
      return k_unit_err_geometry;

    // If SDRAM buffers are required they must be allocated here
    if (!desc->hooks.sdram_alloc)
      return k_unit_err_memory;
    float *m = (float *)desc->hooks.sdram_alloc(BUFFER_LENGTH*sizeof(float));
    if (!m)
      return k_unit_err_memory;

    // Make sure memory is cleared
    buf_clr_f32(m, BUFFER_LENGTH);
    
    allocated_buffer_ = m;

    // Cache the runtime descriptor for later use
    runtime_desc_ = *desc;

    // Make sure parameters are reset to default values
    params_.reset();
    
    return k_unit_err_none;
  }

  inline void Teardown() {
    // Note: buffers allocated via sdram_alloc are automatically freed after unit teardown
    // Note: cleanup and release resources if any
    allocated_buffer_ = nullptr;
  }

  inline void Reset() {
    // Note: Reset effect state, excluding exposed parameter values.
  }

  inline void Resume() {
    // Note: Effect will resume and exit suspend state. Usually means the synth
    // was selected and the render callback will be called again

    // Note: If it is required to clear large memory buffers, consider setting a flag
    //       and trigger an asynchronous progressive clear on the audio thread (Process() handler)
  }

  inline void Suspend() {
    // Note: Effect will enter suspend state. Usually means another effect was
    // selected and thus the render callback will not be called
  }

  /*===========================================================================*/
  /* Other Public Methods. */
  /*===========================================================================*/

  fast_inline void Process(const float * in, float * out, size_t frames) {
    const float * __restrict in_p = in;
    float * __restrict out_p = out;
    const float * out_e = out_p + (frames << 1);  // assuming stereo output

    // Get pitch mode
    int semitone_range = 0; // free hz repitching
    switch (params_.param4) 
    {
      case 1: semitone_range = 7; break;
      case 2: semitone_range = 12; break;
      case 3: semitone_range = 24; break;
    }

    // Get playback mode - continuous or touch-controlled
    switch (params_.param5)
    {
      // Only override this if play mode is auto
      case 0: touchEngaged = true; break;
    }

    const float drift = params_.param6 / 1000.0f;

    // Get mode and playback speed from params
    const float x = params_.param1; // X-axis
    const float y = params_.param2; // Y-axis

    // Default: continuous pitch control
    float playbackSpeedL = fastpowf(2.0f, (x - 0.5f) * 4.0f);
    float playbackSpeedR = playbackSpeedL;

    if (semitone_range > 0)
    {
      // Calculate the total number of steps in the semitone range
      int semitone_steps = semitone_range * 2 + 1; // Example: 25 steps for ±12 semitones

      // Convert the Y-axis value (0.0 to 1.0) to a semitone step
      int step = (int)(x * semitone_steps);  // 0 to 24 for ±12 range
      if (step >= semitone_steps) step = semitone_steps - 1; // Clamp the value to avoid overflow

      // Calculate the semitone offset relative to 0 (which is in the middle)
      int semitoneOffset = step - semitone_range; // Range from -12 to +12

      // Apply the semitone offset to calculate playback speed
      playbackSpeedL = powf(2.0f, semitoneOffset / 12.0f); // Use 12 for semitone increments
      playbackSpeedR = playbackSpeedL;
    }

    const bool shouldRecord = (y > 0.5f);
    const bool shouldPlay = !shouldRecord;

    if (shouldRecord && !isRecording) 
    {
      bufferWritePos = 0;
      bufferLength = 0;
      isRecording = 1;
      isPlaying = 0;

    } 
    else if (shouldPlay && !isPlaying) 
    {
      bufferReadPosL = 0.0f;
      bufferReadPosR = 0.0f;
      bufferLengthL = bufferLength;
      bufferLengthR = bufferLength;
      isPlaying = 1;
      isRecording = 0;      
 
      // init grains
      for(int i = 0; i < MAX_GRAINS; i++)
      {
        grains[i].startOffset = 0.f;
        grains[i].loopLength = bufferLength;
        grains[i].readPos = grains[i].startOffset;
        grains[i].speed = playbackSpeedL;
        grains[i].pan = fast_randf(-1.0f, 1.0f);
        //grains[i].pan = -1.0f + 2.0f * (float)i / (MAX_GRAINS - 1);  // Evenly spaced from -1 to +1
      }
    }

    else if (isPlaying)
    {
      for(int i = 0; i < MAX_GRAINS; i++)
      {
        grains[i].speed = playbackSpeedL;
      }
    }
    

    for (; out_p != out_e; in_p += 2, out_p += 2) 
    {
      // Process samples here      

      float outL = in_p[0];
      float outR = in_p[1];

      if (isRecording) {
        allocated_buffer_[bufferWritePos * 2 ] = outL;
        allocated_buffer_[bufferWritePos * 2 + 1] = outR;
        bufferWritePos = (bufferWritePos + 1) % BUFFER_LENGTH;
        bufferLength = MIN((float)(bufferLength + 1), (float)BUFFER_LENGTH);
      }
      
      if (touchEngaged && isPlaying && bufferLength > 1) {
        
        if (grainModeEnabled) 
        {
          for (int i = 0; i < MAX_GRAINS; ++i) 
          {
              Grain* g = &grains[i];
       
              float readPos = g->readPos;
              int indexA = (int)floorf(readPos);
              int indexB = (indexA + 1) % bufferLength;
       
              // Sample interpolation for both channels
              float sA_L = allocated_buffer_[indexA * 2];
              float sA_R = allocated_buffer_[indexA * 2 + 1];
              float sB_L = allocated_buffer_[indexB * 2];
              float sB_R = allocated_buffer_[indexB * 2 + 1];
       
              float frac = readPos - (float)indexA;
              float interpL = sA_L + frac * (sB_L - sA_L);
              float interpR = sA_R + frac * (sB_R - sA_R);
       
              // Mix the left and right channels to mono (sum them)
              float grainMono = (interpL + interpR) * 0.5f; // Mono grain mix
       
              // Pan control: Apply panning after mixing to mono
              float pan = g->pan * 0.5f + 0.5f; // Normalize pan
              float gainL = cosf(pan * M_PI_2);  // Left channel gain
              float gainR = sinf(pan * M_PI_2);  // Right channel gain
       
              // Apply panning to the mono grain mix
              outL += grainMono * gainL * ((1.0f / MAX_GRAINS) * (MAX_GRAINS / 2.0f));
              outR += grainMono * gainR * ((1.0f / MAX_GRAINS) * (MAX_GRAINS / 2.0f));
       
              // Update read position and apply wrapping logic
              g->readPos = MIN(MAX(g->readPos, 0.0f), (float)(bufferLength - 1));
       
              bool wrapped = false;
       
              g->readPos += g->speed;
              if (g->readPos >= g->startOffset + g->loopLength) 
              {
                  g->readPos -= g->loopLength;
                  wrapped = true;
              } else if (g->readPos < g->startOffset) 
              {
                  g->readPos += g->loopLength;
                  wrapped = true;
              }
       
              // Randomise read positions and loop length on wrap
              if (wrapped && bufferLength > 1) 
              {
                g->loopLength = fast_rand_u32_range((int)bufferLength / 2.0f, bufferLength);
                if (g->loopLength < 16.f) 
                {
                    g->loopLength = 16.f;
                }
                g->readPos = fast_randf(0.0f, g->loopLength - 4.f);
              }

              if(shouldRandomise)
              {                
                // Drift controls speed randomisation range
                g->speed += fast_randf(0.0f - drift, drift);
              }                            
       
              
          }
        }
        else
        {
          int indexAL = (int)floorf(bufferReadPosL);
          int indexBL = (indexAL + 1) % bufferLength;

          int indexAR = (int)floorf(bufferReadPosR);
          int indexBR = (indexAR + 1) % bufferLength;
      
          // If the buffer length is zero, reset read position to a safe state
          if (bufferLength == 0) 
          {
            bufferReadPosL = 0.0f;
            bufferReadPosR = 0.0f;
          }

          float sAL = allocated_buffer_[indexAL * 2];
          float sAR = allocated_buffer_[indexAR * 2 + 1];
          float sBL = allocated_buffer_[indexBL * 2];
          float sBR = allocated_buffer_[indexBR * 2 + 1];
      
          float fracL = bufferReadPosL - (float)indexAL;
          float fracR = bufferReadPosR - (float)indexAR;
          float interpL = fracL * (sBL - sAL);
          float interpR = fracR * (sBR - sAR);
          // Mixes live input with sample playback - defeat with 'MUTE' button on hardware
          outL = (outL + sAL + interpL);
          outR = (outR + sAR + interpR);
      
          // Ensure bufferLength is always valid (clamp it)
          bufferLength = MIN((float)bufferLength, (float)BUFFER_LENGTH);

          // Clamp sub-loop length bounds
          bufferLengthL = MIN(bufferLengthL, bufferLength);
          bufferLengthR = MIN(bufferLengthR, bufferLength);

          bool wrappedL = false;
          bool wrappedR = false;

          bufferReadPosL += playbackSpeedL;
          if (bufferReadPosL >= bufferLengthL) 
          {
            bufferReadPosL -= bufferLengthL;
            wrappedL = true;
          }
          else if (bufferReadPosL < 0.0f) 
          {
            bufferReadPosL += bufferLengthL;
            wrappedL = true;
          }

          bufferReadPosR += playbackSpeedR;
          if (bufferReadPosR >= bufferLengthR) 
          {
            bufferReadPosR -= bufferLengthR;
            wrappedR = true;
          }
          else if (bufferReadPosR < 0.0f) 
          {
            bufferReadPosR += bufferLengthR;
            wrappedR = true;
          }

          if(shouldRandomise)
          {
            // Only randomise read position on wrap
            if (wrappedL && bufferLength > 1) 
            {
              bufferLengthL = fast_rand_u32_range(4, bufferLength);
              bufferReadPosL = fast_randf(0.0f, (float)bufferLength);
            }
            if (wrappedR && bufferLength > 1) 
            {
              bufferLengthR = fast_rand_u32_range(4, bufferLength);
              bufferReadPosR = fast_randf(0.0f, (float)bufferLength);
            }

            // Drift controls speed randomisation range
            playbackSpeedL += fast_randf(0.0f - drift, drift);
            playbackSpeedR += fast_randf(0.0f - drift, drift);
          }

        } // if grainenabled ends
          
      }

      out_p[0] = outL;
      out_p[1] = outR;      
      
    } // main sample processing loop ends
    

  }

  inline void setParameter(uint8_t index, int32_t value) {
    switch (index) {
    case PARAM1:
      // 10bit 0-1023 parameter
      value = clipminmaxi32(0, value, 1023);
      params_.param1 = param_10bit_to_f32(value); // 0 .. 1023 -> 0.0 .. 1.0
      break;

    case PARAM2:
      // 10bit 0-1023 parameter
      value = clipminmaxi32(0, value, 1023);
      params_.param2 = param_10bit_to_f32(value); // 0 .. 1023 -> 0.0 .. 1.0
      break;

    case DEPTH:
      // Single digit base-10 fractional value, bipolar dry/wet
      value = clipminmaxi32(-1000, value, 1000);
      params_.depth = value / 1000.f; // -100.0 .. 100.0 -> -1.0 .. 1.0
      break;

    case PITCHMODE:
      // strings type parameter, receiving index value
      value = clipminmaxi32(PARAM4_VALUE0, value, NUM_PARAM4_VALUES-1);
      params_.param4 = value;
      break;

    case PLAYMODE:
      // strings type parameter, receiving index value
      value = clipminmaxi32(PARAM5_VALUE0, value, NUM_PARAM5_VALUES-1);
      params_.param5 = value;
      break;

    case DRIFT:
      // Single digit base-10 0-99
      value = clipminmaxi32(0, value, 99);
      params_.param6 = value;
      break;
      
    default:
      break;
    }
  }

  inline int32_t getParameterValue(uint8_t index) const {
    switch (index) {
    case PARAM1:
      // 10bit 0-1023 parameter
      return param_f32_to_10bit(params_.param1);
      break;

    case PARAM2:
      // 10bit 0-1023 parameter
      return param_f32_to_10bit(params_.param2);
      break;

    case DEPTH:
      // Single digit base-10 fractional value, bipolar dry/wet
      return (int32_t)(params_.depth * 1000);
      break;

    case PITCHMODE:
      // strings type parameter, return index value
      return params_.param4;

    case PLAYMODE:
      // strings type parameter, return index value
      return params_.param5;

    case DRIFT:
      // strings type parameter, return index value
      return params_.param6;
      
    default:
      break;
    }

    return INT_MIN; // Note: will be handled as invalid
  }

  inline const char * getParameterStrValue(uint8_t index, int32_t value) const {
    // Note: String memory must be accessible even after function returned.
    //       It can be assumed that caller will have copied or used the string
    //       before the next call to getParameterStrValue
    
    static const char * param4_strings[NUM_PARAM4_VALUES] = {
      "FREE HZ",
      "7 SEMI",
      "12SEMI",
      "24SEMI",
    };

    static const char * param5_strings[NUM_PARAM5_VALUES] = {
      "AUTOPLAY",
      "TOUCH",      
    };
    
    switch (index) 
    {
      case PITCHMODE:
        if (value >= PARAM4_VALUE0 && value < NUM_PARAM4_VALUES)
          return param4_strings[value];
        break;
      case PLAYMODE:
        if (value >= PARAM5_VALUE0 && value < NUM_PARAM5_VALUES)
          return param5_strings[value];
        break;
      default:
        break;
    }
    
    return nullptr;
  }
  
  inline void setTempo(uint32_t tempo) {
    // const float bpmf = (tempo >> 16) + (tempo & 0xFFFF) / static_cast<float>(0x10000);
    (void)tempo;
  }

  inline void tempo4ppqnTick(uint32_t counter) {
    (void)counter;
  }

  inline void touchEvent(uint8_t id, uint8_t phase, uint32_t x, uint32_t y) {
    // Note: Touch x/y events are already mapped to specific parameters so there is usually there no need to set parameters from here.
    //       Audio source type effects, for instance, may require these events to trigger enveloppes and such.
    
    (void)id;
    (void)phase;
    (void)x;
    (void)y;
    
    switch (phase) 
    {
      case k_unit_touch_phase_began:
        // Only randomise L/R playheads if recording is started in the upper-right two thirds of the touchpad
        shouldRandomise = (x >= 341) && (y >= 512);
        // Only enable granular if recording is started in the upper-right one third of the touchpad
        if((x >= 682) && (y >= 512))
        {
          grainModeEnabled = true;
        }
        // Only disable grain mode if one of the other modes was initiated (so the lower half can be 
        // repeatedly touched for pitch changes without disabling grain mode)
        if((x < 682) && (y >= 512))
        {
          grainModeEnabled = false;
        }
        touchEngaged = true;
        break;
      case k_unit_touch_phase_moved:
        break;
      case k_unit_touch_phase_ended:
        touchEngaged = false;
        break;  
      case k_unit_touch_phase_stationary:
        break;
      case k_unit_touch_phase_cancelled:
        touchEngaged = false;
        break; 
      default:
        break;
    }
  }
  
  /*===========================================================================*/
  /* Static Members. */
  /*===========================================================================*/
  
 private:
  /*===========================================================================*/
  /* Private Member Variables. */
  /*===========================================================================*/

  std::atomic_uint_fast32_t flags_;

  unit_runtime_desc_t runtime_desc_;

  Params params_;
  
  float * allocated_buffer_;

  Grain grains[MAX_GRAINS];
  
  /*===========================================================================*/
  /* Private Methods. */
  /*===========================================================================*/

  /*===========================================================================*/
  /* Constants. */
  /*===========================================================================*/
};
