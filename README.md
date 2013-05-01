balance.lv2 -- LV2 stereo balance
=================================

balance.lv2 is an audio-plugin for stereo balance control with
optional per channel delay.

balance.lv2 facilitates adjusting stereo-microphone recordings (X-Y, A-B, ORTF).
But it also generally useful as "Input Channel Conditioner".
It allows for attenuating the signal on one of the channels as well as
delaying the signals (move away from the microphone).
To round off the feature-set channels can be swapped or the signal can be
downmixed to mono after the delay.

Install
-------

```bash
  git clone git://github.com/x42/balance.lv2.git
  cd balance.lv2
  make
  sudo make install PREFIX=/usr
  jalv.gtk http://gareus.org/oss/lv2/balance
```

Signal Flow
-----------

![signal flow](https://raw.github.com/x42/balance.lv2/master/doc/signal_flow.png "Signal Flow")

The plugin has six control inputs.
All except the 'channel map' are smoothed and can be automated or changed dynamically without introducing clicks.

### Trim

Simple gain stage to amplify or attenuate the signal by at most 20dB. The control affects both channels.

### Balance

Left/Right signal level balance control.

* classic "Balance" mode.
  * 100% left, mutes the right channel, left channel is untouched.
  *  50% left, attenuates the right channel by -6dB (signal * 0.5), left channel is untouched.
  *  29% left, attenuates the right channel by -3dB (signal * 0.71), left channel is untouched.
  *  50% right, attenuates the left channel by -6dB (signal * 0.5), right channel is untouched.
  * ...
* "Unity Gain - Equal Amplitude" mode
  * behaviour of the attenuated channel is identical to "balance" mode
  * gain of the previously untouched channel is raised so that the mono sum of both has equal amplitude
  * 100% right: -inf dB on left channel and +6dB on right channel.
  *  29% left: -3.0 dB on left channel and +2.2dB on right channel.
* "Seesaw - Equal Power" mode
  * -6dB .. +6dB range, equal power distribution
  * The signal is at full-level in center position and becomes progressively louder as it is panned to the right or left.
  * 100% right: -6dB on left channel and +6dB on right channel.
  *  29% right: -3dB on left channel and +3dB on right channel.
  * Note, the overall slider range is identical to the other modes.
    Values > 50%..100% in either direction are fixed to +-6dB.

### Delay

Allow to delay the signal of either channel to correct the stereo field (signal runtime) or correct phase alignment.

### Channel Map

Routing options. NB. Currently there is no de-clicking if the routing changes.
Mute or disconnect the input when changing this setting or live with potential clicks.

The "Downmix to Mono" option will attenuate the output by -6dB. Other options will simply copy
the result to selected channel(s).

Screenshot
----------

![screenshot](https://raw.github.com/x42/balance.lv2/master/doc/screenshot.png "Example running in Ardour")

Thanks
------
Many thanks to all who contributed ideas and feedback. In particular
Chris 'oofus' Goddard, who inspired the current signal flow, provided
the diagram and beta-tested the plugin.