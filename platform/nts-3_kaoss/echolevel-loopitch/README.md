# echolevel-loopitch
 Pitch adjustable looper effect for Korg NTS-3 Kaoss Pad

 See and hear it on [youtube](https://www.youtube.com/watch?v=6W8OzDvJrg0)

## Build
Set up Docker environment as detailed in logue-sdk docs. From logue-sdk env command line, run 

`build -f nts-3_kaoss/echolevel-loopitch` 

to force a clean then build __echolevel_loopitch.nts3unit__.

## Install

Connect to the NTS-3 Kaoss Pad via USB and the Korg Kontrol Editor. Go to Generic FX then drag and drop 
__echolevel_loopitch.nts3unit__ onto a slot. Now you can select it as one of the 4 effects in a Program. 

## Use

Press and hold on the top half of the X/Y pad to start sampling incoming audio.

Slide to the bottom half of the pad to start playback, then drag left or right to adjust playback speed/pitch.

Depending on which 3rd of the pad's top half you initially press (left, middle or right), the playback mode will be:

* 'Normal' - left and right stereo channels both playing at a synchronised rate from identical buffer offsets. Use this mode to resample back to a vintage sampler which lacks the ability to resample internally.
* 'Grain Drift' - the Drift parameter controls the amount by which left and right channels are semi-randomly offset and rate adjusted to give a wide, unpredictable, slightly glitchy effect; and the number of granular voices is controlled by the FX Depth fader from 2 to 24. If the voice count is 2, the grains are hard-panned left and right. For higher counts, panning is distributed evenly across the stereo field. Starting offsets and loop points within each grain are also randomised per loop.
* 'Grain Octave Drift' - as above, except each grain has a 50% chance of being played an octave above or below the current playback centre pitch.

Pitch modes are as follows:
* Continuous pitch - the full resolution of the X/Y pad is used to gradually adjust the sample playback rate and thus the speed/pitch of the output
* 7 semitones - the playback rate is quantised so that semitone pitch adjustments are possible. 7 semitones allows precise control over a limited range.
* 12 semitones - slightly less precise control over a 2-octave range (12 below and 12 above the central sampling rate)
* 24 semitones - 4 octave range with considerably less precise control

Touch modes:
* 'Auto' - the sample loops indefinitely, producing constant output of the buffer's contents
* 'Touch' - output is only heard when the pad is touched, allowing it to be used like a musical keyboard

## Why?
I love [the Yamaha SU10 sampler](https://www.youtube.com/watch?v=muO-xxlZpMg). It's incredibly limited, due in no small part to some wildly dumb design decisions, but most of them add to its charm. It's very frustrating, however, that it can't resample internally - you have to bounce out to some recording device and then sample in again. I've always used the SU10 with a Mini Kaoss Pad, so when the NTS-3 came along and offered an SDK for custom effects it seemed obvious to write one that does this one basic thing - sampling input then playing it back at an arbitrary rate without any fancy algorithmic pitch/formant shifting. That's 'normal' mode; the other two modes are just for fun. They sound great on some sources, especially with some delay or reverb.

## Bugs and To Do
Sometimes the effect edit menu stops working (Edit+FX buttons). This might be due to some wonky param enum setup, or there could be something gnarlier going on with buffer memory. I feel like it only started happening after I added the granular mode, but I'll try to pin it down.

There's nothing really I want to add to the effect - it's a handy utility for me in a chain and it lets me work with the SU10 the way I want.