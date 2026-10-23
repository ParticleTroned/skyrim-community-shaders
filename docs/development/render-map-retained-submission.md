# Retained native eye submission evidence

The native submission caller retains the active RenderMap capture generation
before invoking the compositor. A zero generation never becomes eligible after
a capture starts. Recording requires the same nonzero generation at resource
observation, event publication and accepted-eye completion.

Render-target recreation is excluded by one shared-lock interval spanning the
final publication/device sample, retained-texture description, CPU-frame sample
and Runtime record. The retained lease must still match that sampled publication.
The lock is acquired after the compositor returns and is not held across native
Submit. The guarded observer is synchronous and must not re-enter recreation.

The affected Runtime host tests use the actual shared guard and Runtime/Collector
paths. They cover stable success, stopped capture, stop/start turnover, start
from zero, turnover between eyes, a competing exclusive recreation attempt during
sampling, and stale lease rejection after recreation. They do not invoke OpenVR,
D3D or the game, and do not qualify installed hooks or GPU behavior.
