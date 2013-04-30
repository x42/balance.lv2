balance.lv2 -- LV2 stereo balance
---------------------------------

balance.lv2 is an audio-plugin for stereo balance control with
optional per channel delay.

balance.lv2 is intended to allow adjusting stereo-microphone
recordings (X-Y, A-B, ORTF). It allows for attenuating the signal on one of
the channels as well as delaying the signals (move away from the microphone).


Install
-------

```bash
  git clone git://github.com/x42/balance.lv2.git
  cd balance.lv2
  make
  sudo make install PREFIX=/usr
  jalv.gtk http://gareus.org/oss/lv2/balance
```

Screenshot
----------

![screenshot](https://raw.github.com/x42/balance.lv2/master/doc/balance_lv2.png "Example running in Ardour")
