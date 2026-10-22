# Rider effort from the PAS sensor

Can a BBSHD, which only has a cadence (PAS) sensor, tell how hard the rider is pushing?

## The idea

Each leg pushes hardest about halfway down, so rider torque peaks twice per crank revolution. With
the chain engaged, that pulsing torque makes the bike, and therefore the crank, speed up and slow
down slightly twice per revolution. The size of that wobble is proportional to the rider's torque.

- **Physics.** For a crank wobble Δω at twice the crank speed ω, the torque swing is
  ΔT = Δω · I_eff · 2ω, where I_eff = m (r_wheel · gear)² is the bike's mass seen from the crank.
  For a pedal stroke whose torque swings by a fraction `a` of its mean (about 0.8), the mean
  torque is ΔT / a, and rider power is mean torque × ω.
- **Prior art.** Sensorless pedal-torque estimation is an active research topic, mostly using
  observers or Kalman filters on a bike model. A European patent
  ([EP2566748B1](https://data.epo.org/gpi/EP2566748B1)) covers estimating e-bike pedal torque
  from the AC component of crank speed, which is this idea.

## What the model says (`ripple_model.c`, `learn.sh`)

The model simulates the bike and rider at 10 µs steps and measures PAS edges exactly as
`bbsx/sensors.c` does (first 100 µs tick after each edge). The error sources it includes:

- 0.3° random magnet spacing errors
- 10 N of road noise
- leg inertia
- ±10% wander in the rider's effort

With the magnet pattern learned while cruising, effort is estimated per 4 revolutions:

| Situation | Speed | True 10 / 25 / 40 Nm reads as |
|---|---|---|
| Low gears: pulling away, climbing | 5–13 km/h | 9 / 24 / 39 |
| Mid gears | 13–20 km/h | 8 / 22 / 37 |
| Brisk cruising | 20–27 km/h | 7 / 19 / 33 |
| Fast, top gears | 30–40 km/h | 10 / 11 / 22 (light efforts lost) |

What made it work, in order of importance:

1. **Timing all four edges of both PAS channels**: 96 timestamps per revolution instead of 24.
   On its own, a 50 µs tick instead of 100 µs made no difference.
2. **Learning the magnet pattern.** Uneven magnet spacing (0.3° is 8% of a 3.75° segment) dwarfs
   the wobble at higher gears. The rider's wobble is locked to crank angle too, so it can't simply
   be averaged away. But it shrinks with gear² × cadence², so while cruising fast in a high gear
   the measured pattern is almost purely the magnets'. The pattern is learned there and used
   everywhere.
3. **Removing the mean before the harmonic.** With unevenly spaced samples, the large mean speed
   otherwise leaks into the 2nd harmonic.

Road noise up to 20 N and the effort wander made no material difference.

## Unknowns that only the bike can answer

- Whether the real PAS1/PAS2 edges are stable enough to learn: the duty cycle and phase can be
  anything, as long as they're repeatable.
- Whether the motor's own controller smooths torque wobble. It's modelled as a constant push.
- The rider's stroke shape `a`. It scales the estimate, not whether it tracks effort.
- Drivetrain compliance and tyre effects, which aren't modelled.

The firmware shows its estimate live in the display's Range field. That's the experiment that
settles these: ride, push harder, and see whether the number follows.

## What the firmware added (`src/firmware/effort.c`)

Running the estimator inside the ride simulator, with motor power feeding back into the bike,
showed three things the steady-state model above didn't:

1. **Speeding up leaks into the harmonic.** A speed trend across the one-revolution window looks
   like wobble: a 150 W rider read as 700-1100 W while the motor ramped up, a runaway. The trend
   is measured from the window's speed sum half a revolution apart and its exact leak (it depends
   only on where the window starts) is subtracted.
2. **Gear changes and hard launches aren't linear trends.** When the speed moves more than 5% in
   half a revolution, or 2.5% in a quarter, the last estimate is held.
3. **The rider's own wobble while learning ends up in the pattern.** It's locked to the crank
   just like the magnet errors, so it's subtracted from every later reading: about the learning
   power × (v²ω now ÷ v²ω while learning). The firmware stores v²ω with the pattern and adds back
   an assumed 100 W. Reading this properly from the data is the next step if the ride test works.

With those, the simulator's learn-then-climb scenario (`host/sim/scenarios/effort_assist.sim`)
reads 100 W as ~97 W and 250 W as ~240 W.
