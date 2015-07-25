balance.lv2 - LV2 stereo balance
================================

balance.lv2 is an audio-plugin for stereo balance control with
optional per channel delay.

balance.lv2 facilitates adjusting stereo-microphone recordings (X-Y, A-B, ORTF).
But it also generally useful as "Input Channel Conditioner".
It allows for attenuating the signal on one of the channels as well as
delaying the signals (move away from the microphone).
To round off the feature-set channels can be swapped or the signal can be
downmixed to mono after the delay.

It features a Phase-Correlation meter as well as peak programme meters
according to IEC 60268-18 (5ms integration, 20dB/1.5 sec fall-off)
for input and output signals.

Install
-------

Compiling this plugin requires LV2 SDK (lv2 lv2core), gnu-make and a c-compiler.
The optional UI depends on libftgl-dev, libglu-dev, libx11-dev and the
fonts-freefont-ttf (or any other .ttf font).

```bash
  git clone git://github.com/x42/balance.lv2.git
  cd balance.lv2
  make
  sudo make install PREFIX=/usr
  
  # test run
  jalv.gtk http://gareus.org/oss/lv2/balance
```

Note to packagers: The Makefile honors `PREFIX` and `DESTDIR` variables as well
as `CFLAGS`, `LDFLAGS` and `OPTIMIZATIONS` (additions to `CFLAGS`).

Signal Flow & Controls
----------------------

![signal flow](https://raw.github.com/x42/balance.lv2/master/doc/signal_flow.png "Signal Flow")

The plugin has six control inputs, all of which are interpolated and can
be automated or changed dynamically without introducing clicks.

### Trim

Simple gain stage to amplify or attenuate the signal by at most 20dB.
This knob equally affects both channels.

This stage also allows to individually invert the phase of each channel.

### Balance

Left/Right signal level balance control.
*Gain Mode* defines the behaviour of the *Balance* knob.

* classic "Balance" mode.
  * Attenuate one channel at a time; no positive gain.
  * 100% left: mute the right channel, left channel is untouched.
  *  50% left: attenuate the right channel by -6dB (signal * 0.5), left channel is untouched.
  *  29% left: attenuate the right channel by -3dB (signal * 0.71), left channel is untouched.
  *  50% right: attenuate the left channel by -6dB (signal * 0.5), right channel is untouched.
  * ...
* "Unity Gain - Maintain Amplitude" mode
  * behaviour of the attenuated channel is identical to "Balance" mode.
    Gain of the previously untouched channel is raised so that the mono sum of both retains equal amplitude.
  * 100% right: -inf dB on left channel (signal * 0.0), +6dB on right channel (signal * 2.0).
  *  50% left: attenuate the right channel by -6dB (signal * 0.5), left channel is amplified by +3.5dB (signal * 1.5)
  *  29% left: -3.0 dB (signal * .71) on left channel,  +2.2dB (signal * 1.29) on right channel.
  * ...
* "Seesaw - Equal Power" mode
  * -6dB .. +6dB range, equal power distribution
  * The signal is at full-level in center position and becomes progressively louder as it is panned to the right or left.
  * The overall slider range is identical to the other modes.
    Values > 50%..100% in either direction are fixed to +-6dB.
  * 100% right: -6dB on left channel and +6dB on right channel.
  *  50% right: -6dB on left channel and +6dB on right channel (!).
  *  29% right: -3dB on left channel and +3dB on right channel.

Regardless of the Gain Mode, at center position the signal remains unmodified.

### Delay

Allow to delay the signal of either channel to correct the stereo field (signal runtime) or correct phase alignment.

### Channel Map

The "Downmix to Mono" option will attenuate the output by -6dB. Other options will simply copy
the result to selected channel(s).

Screenshots
-----------
The plugin comes with a built-in optional user interface.

![screenshot](https://raw.github.com/x42/balance.lv2/master/doc/screenshot_ui.png "Built-in openGL GUI")
![screenshot](https://raw.github.com/x42/balance.lv2/master/doc/screenshot_ardour.png "Basic controls in Ardour")

Thanks
------
Many thanks to all who contributed ideas and feedback. In particular
Chris 'oofus' Goddard, who inspired the current signal flow, provided
the diagram and beta-tested the plugin. As well as tom^_ for feedback
on the GUI in general.
