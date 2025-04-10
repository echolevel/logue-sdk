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
 *  Dummy generic effect template instance.
 *
 */

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <climits>

#include "unit_genericfx.h"   // Note: Include base definitions for genericfx units

#include "utils/buffer_ops.h" // for buf_clr_f32()
#include "utils/int_math.h"   // for clipminmaxi32()

class Effect {
	public:	
  /*===========================================================================*/
  /* Public Data Structures/Types/Enums. */
  /*===========================================================================*/

	float inv[2] = {0.f};
	//float RInv = 0.f;
	double ffspeed = 0.0;
	float egAttack = 0.f;
	float egRelease = 0.f;
	float ffrange = 0.f;
	bool trigger = 0;
	int egstate = 0;
	double eg = 0.0;
	double peak = 0.0;
	double peakeg = 0.0;
	float sig[2] = {0.f};

  enum {
    BUFFER_LENGTH = 0x40000 
  };

  enum {
    PARAM1 = 0U,
    PARAM2,
    DEPTH,
    PARAM4, 
    NUM_PARAMS
  };

  // Note: Make sure that default param values correspond to declarations in header.c
  struct Params {
    float param1{0.f};
    float param2{0.f};
    float depth{0.f};
    uint32_t param4{1};

    void reset() {
      param1 = 0.f;
      param2 = 0.f;
      depth = 0.f;
      param4 = 1;
    }
  };

  enum {
    PARAM4_VALUE0 = 0,
    PARAM4_VALUE1,
    PARAM4_VALUE2,
    PARAM4_VALUE3,
    NUM_PARAM4_VALUES,
  };
  
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

    // Caching current parameter values. Consider interpolating sensitive parameters.
    // const Params p = params_;
    
    for (; out_p != out_e; in_p += 2, out_p += 2) {
    	// Process samples here
		inv[0] = -in_p[0];
		inv[1] = -in_p[1];
		ffspeed = 0.99999 - ((params_.param2*params_.param2 / k_samplerate) * 30000.0);	//param2は0.5~1
		peakeg = 1.0 - (1.0 / k_samplerate) * 10.0;
		egAttack = float(ffspeed);
		egRelease = float(ffspeed);
		ffrange = params_.param1 * 0.6f;

		//ff THD
		float stereoLevel = (in_p[0] + in_p[1]) * 0.5f;
		if(abs(stereoLevel) > (ffrange * peak)){
			trigger = 0;
		} else {
			trigger = 1;
		}

		//eg calc
		if(trigger == 1){
			eg = 1.0 - (1.0 - eg) * egAttack;
		} else {
			eg = eg * egRelease;
		} 

		//peak meter calc
		if(abs(inv[0])>peak)	peak = double(abs(inv[0]));
		if(abs(inv[1])>peak)	peak = double(abs(inv[1]));
		peak = peak * peakeg;

		//mixer
		sig[0] = in_p[0] * (1.f - eg) + inv[0] * eg;
		sig[1] = in_p[1] * (1.f - eg) + inv[1] * eg;

		//depth
		out_p[0] = in_p[0] * (1.f - params_.depth) + sig[0] * params_.depth;
		out_p[1] = in_p[1] * (1.f - params_.depth) + sig[1] * params_.depth;
    }
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
      value = clipminmaxi32(0, value, 1000);
      params_.depth = value / 1000.f;
      break;

    case PARAM4:
      // strings type parameter, receiving index value
      value = clipminmaxi32(PARAM4_VALUE0, value, NUM_PARAM4_VALUES-1);
      params_.param4 = value;
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

    case PARAM4:
      // strings type parameter, return index value
      return params_.param4;
      
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
      "VAL 0",
      "VAL 1",
      "VAL 2",
      "VAL 3",
    };
    
    switch (index) {
    case PARAM4:
      if (value >= PARAM4_VALUE0 && value < NUM_PARAM4_VALUES)
        return param4_strings[value];
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
    
    // switch (phase) {
    // case k_unit_touch_phase_began:
    //   break;
    // case k_unit_touch_phase_moved:
    //   break;
    // case k_unit_touch_phase_ended:
    //   break;  
    // case k_unit_touch_phase_stationary:
    //   break;
    // case k_unit_touch_phase_cancelled:
    //   break; 
    // default:
    //   break;
    // }
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
  
  /*===========================================================================*/
  /* Private Methods. */
  /*===========================================================================*/

  /*===========================================================================*/
  /* Constants. */
  /*===========================================================================*/
};
