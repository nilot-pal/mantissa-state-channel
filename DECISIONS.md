# Decisions

What was tried, what it cost, and why each choice was made. Written while building, in order.

## 27th July 2026

### Tests 1 and 7 before the implementation

The two tests were written first and the header was written against them. Test 1 fixes what the
channel must preserve exactly, test 7 fixes what it is allowed to disturb. Everything else in the
design was settled by making those two pass.

The order mattered more than expected. Writing test 7 first is what forced the error bound to be a
named constant derived from the payload width rather than a number typed into an assertion, because
there was nothing to type in yet.

### Two layers, not one

The channel and the quantisation are separate: `pack` and `unpack` move an integer code through the
mantissa, and `log_scale` turns a state variable into a code.

They were one thing at first. Splitting them was worth it because the two have different failure
modes and different tests. The channel either recovers the exact bits or it does not. The scale is
lossy by construction and its test is about resolution and clamping. Fused, test 1 could not have
been exhaustive: it would have been a statement about floating point round tripping rather than
about the bit field.

The scale range is a constructor argument, not a constant. The production range is a property of the
model and does not belong in a public repo.

### No test framework

A counting harness in forty lines, in `tests/check.hpp`. The exhaustive tests run millions of
assertions, so the requirement is that a failure prints once and the rest are counted, which is not
what a framework built around per-case reporting does well. Adding GoogleTest would also have added
a dependency and a build step to a repo whose point is that it is readable in ten minutes.

### The bound is not a truncation, and it is not symmetric

This was the first real finding, and it contradicts what the README said.

The field is masked away and then the flag and the payload are written back into it. The flag is the
top bit of the field, so the payload occupies the upper half of the truncation interval and the
packed carrier is skewed upward. In units of the last mantissa bit the movement lies in
[-16383, +32767], not in [-32767, 0].

So the packed diameter can be higher than the true one, and the earlier description of it as
truncating downward was wrong in direction as well as in symmetry. The magnitude bound, 2 to the
power minus 8 and about 0.39 per cent, was right.

Both edges are now constants in the header, `carrier_error_above` and `carrier_error_below`, and
test 7 asserts the signed interval rather than the magnitude. It also constructs both extremes
rather than hoping a sweep lands on them. The upper edge is attained exactly. The lower edge is
approached to within one part in five hundred and cannot be attained, because a carrier with a full
field has a significand slightly above 1 and the relative error is measured against that.

Only `strip` truncates downward, and it is the one path a consumer has to opt into.

### The micrometre figure depends on where the grain sits between two powers of two

The second finding, and the reason test 7 sweeps carriers as well as payloads.

The relative bound is largest at a significand of 1 and falls as the significand rises towards 2, so
the same 0.39 per cent is a different number of micrometres for different grain sizes. A 100
micrometre grain has a significand of 1.6384, and its worst case is 0.238 micrometres. The earlier
figure of roughly 0.4 micrometres for a 100 micrometre grain was the relative bound multiplied
through without accounting for that, and it overstates the error by a factor of about 1.64.

The first version of the test swept payloads against one exact carrier and reported 0.136
micrometres, which was neither the bound nor the worst case, just that carrier's luck. Sweeping
carriers as well brings it to 0.238, which agrees with the analytic value.

The honest statement is therefore two statements. No grain anywhere moves by more than 0.39 per cent
of itself. A 100 micrometre grain moves by at most 0.24 micrometres. Quoting 0.4 micrometres for a
100 micrometre grain is conservative rather than wrong, but it is the sort of number a reviewer
checks.

### memcpy, and why not the alternatives

`reinterpret_cast` between `float*` and `uint32_t*` is a strict aliasing violation and the optimiser
is entitled to act on it. A union is defined behaviour in C and not in C++, whatever every compiler
happens to do. `std::bit_cast` is C++20 and the target is C++17.

`memcpy` is the only portable option left and it costs nothing: at Release both directions compile
to a register move.

### Strict floating point

`/fp:strict` under MSVC, `-ffp-contract=off` elsewhere. Fast maths licences the compiler to assume
the values it is reasoning about are ordinary numbers, which is precisely what this code does not
promise: the guards exist because the inputs include denormals, infinities and NaN.

Not yet verified that the guards survive `/fp:fast`. The expectation is that they do, because the
classification is done on the integer bits and never on the float, but it has not been tested and
the flag is set conservatively until it has been.

### Toolchain

Windows 11, MSVC 19.42 from Visual Studio Community 2022, x64, CMake 3.29.5. No other compiler is
installed on this machine, so the single platform limitation in the README is real and stays until
the repo has been built somewhere else. GCC and Clang would be worth doing before it is pushed,
because a portability claim resting on one compiler is not a portability claim.

Two tests pass, 17.8 million assertions, 0.08 seconds.

### The remaining ten

Added tests 2 to 6, 8 to 12. Twelve pass, 185 million assertions, 0.8 seconds. Three of them turned
up something.

### The flag does not mean what the first draft said it meant

Test 2 was going to assert that there are no false positives. It cannot, because there are.

`has_payload` is a test of one bit in a field this code does not own. An ordinary diameter that was
never packed, whose bit 14 happens to be set, reads as flagged and yields whatever its low bits
contain. It is not a rare corner either: about half of all arbitrary carriers do it, and the test
now measures the rate and asserts it is about half, which is a strange looking assertion until you
read why it is there.

So the flag distinguishes a packed carrier from a carrier this code packed and then cleared. It does
not distinguish a packed carrier from one nobody ever touched. What makes the technique safe in
production is that every particle goes through the injection routine exactly once and is packed
there, so an unflagged carrier is not a thing that occurs. The fallback covers carriers that were
inadmissible at injection, not carriers that were never initialised.

That is a deployment precondition rather than a property of the code, and it was implicit before.
The README now states it under what to distrust, because a reader adopting this would otherwise
find it the hard way.

A wider flag would not fix it, only make it rarer. Two bits would leave a quarter of carriers
falsely flagged, ten bits about one in a thousand, and every bit spent comes straight out of the
carrier. The initialisation pass is the fix, and it is free.

### The benchmark measured the wrong thing twice

The first version compared standalone pack and unpack against a position update and reported that
the pair cost 2.8 times one update. That number was wrong in a way that flattered nothing and
informed nobody: the baseline loop auto-vectorises and the unpack loop, which has a branch in it,
does not. It was a measurement of auto-vectorisation.

The second version put the channel inside the tracking loop and took the difference, which is the
question anyone actually has. It reported 6.9 ns per particle per step, against 0.87 ns for the
vectorised baseline, so nearly eight times one update. Also misleading, for the same reason
inverted: adding the channel stops the loop vectorising, so the difference charges the channel for
the vectorisation as well as for its own work.

The third version measures a scalar baseline as well, and the split is the interesting part:

| | ns per particle per step |
|---|---|
| Position update, auto-vectorised | 0.87 |
| Position update, scalar | 4.39 |
| The same update carrying state | 8.33 |
| Added by the channel | 3.94 |
| Vectorisation forgone | 3.51 |

Reproducible to within about one per cent across runs.

The honest reading is that the channel costs roughly one position update of its own work, and can
cost about the same again in vectorisation if it lands in a loop that was vectorising. A real
tracking loop does a cell search and interpolates the carrier phase per particle and is not
vectorising, so the second half is usually not charged, but that is a claim about someone else's
solver and it belongs in the caveats rather than in the headline.

The standalone figures, 1.05 ns to pack and 1.37 ns to unpack, are kept because they are the ones
that answer whether the arithmetic itself is cheap. It is. Everything above that is the loop, not
the operation.

### What test 5 exercises that the others do not

The exhaustive denormal sweep, all 8,388,607 of them, is the one test here that could not have been
written against the production code, and it is the reason the reimplementation was worth doing at
all rather than just describing the technique. There is no way to feed a commercial solver eight
million malformed diameters and watch what it does with them.

### Figures, and the two that had to be redrawn

The code answers how, and a reader skimming for two minutes wants what for. Three figures, all
generated by running the library rather than drawn, so they cannot quietly disagree with the code.

The bit layout figure was easy and is probably the most useful thing in the README. It replaced a
hand-typed ASCII diagram whose bits were invented. Real bits for a real 100 micrometre grain cost
nothing and say more.

The other two were both wrong the first time, in the same way. The carrier error repeats with a
period of one full field, which at 100 micrometres is 0.24 micrometres, so the first version, which
plotted 60 to 260 micrometres, put 840 cycles into 900 pixels and rendered as a solid green block.
The fix was to stop trying to show two things at once: one panel zoomed to about five periods, where
the sawtooth is visible, and a second panel that gives up the fine structure entirely and plots only
the worst case against size. The second panel is the one that shows the resets at every power of two,
which is the finding from the first day drawn rather than asserted.

The state figure aliased the same way, and the fix was better content rather than a better axis. It
now plots log against linear quantisation on the same 16384 codes. The linear scheme sits at about a
hundred per cent error across the bottom two decades and only overtakes log above a state of about
100. That is the argument for log spacing, which the README had been asserting in one sentence with
nothing behind it.

Both mistakes were the same mistake: plotting the data at a scale where the structure is finer than
a pixel. Worth remembering.

### Where this sits in the solver, which the README was missing entirely

The repo explained the technique and never said what it was for. The wall interaction returns six
values for the child particle: a breakup indicator, the diameter, the number rate, two coefficients
of restitution and a mode. Four are consumed by the rebound and never seen again, one is a
statistical weight, and exactly one is a continuous physical quantity still attached to the particle
at the next impact.

So the diameter is not a convenient carrier, it is the only carrier. That single observation is
what makes the technique look necessary rather than clever, and it was missing. `[VERIFIED against
the production return stack]`

### The applicability envelope, and the failure mode that was missing

Added a section on where the technique is valid, because everything else in the README says what it
costs and nothing said when not to use it.

Two things came out of writing it.

**The payload width had never been justified, only stated.** It is the single real design choice in
the whole thing and the README asserted 14 as though it fell from the sky. Generalising the layout
to any width took four constexpr functions, and the constants that were already there are now those
functions evaluated at 14, with a static assertion that they agree. So the design space figure is
drawn from the same code that derives the shipped bound, and cannot drift away from it.

The curve is more interesting than expected. Carrier cost doubles per bit and state resolution
halves per bit, so on a log axis they are two straight lines and they cross at about 13. Fourteen is
one bit past the crossing. The honest reason to prefer 14 over 13 is that the carrier is measured
against a physical resolution that is fixed and known, while the state is measured against a model
that might be refined later, so the asymmetry is worth a bit. That is a defensible argument and it
is now written down; before it was not an argument at all.

**The failure mode that matters most was not in the repo at all.** The carrier must be written only
by the encoder. If a second model writes the diameter field, the flag bit survives and the payload
becomes arithmetic residue, which is a wrong state wearing the costume of a valid one. Every guard
in here protects against a carrier that cannot hold a payload; nothing protects against a carrier
that was overwritten after it did.

`[VERIFIED]` In production the invariant holds by construction: the child diameter is written at
four sites and all four go through the encode function, and it is read at one, through the decode.
So this was never a live bug. It was an unstated precondition, which is worse in a public repo than
in a private one, because a reader adopting the technique inherits the precondition without being
told it exists.

There is no cheap way to detect the violation from inside the channel. A checksum over the carrier
bits would cost payload and only catch some cases. The honest answer is that it is a deployment
precondition, so the README states it first in the list rather than burying it.

**Corrected the same day. The paragraph above is wrong, and the counterexample is the production
code.** See the next entry.

### The design that shipped is not the design this repo was built from

`Breakage code/pt_breakage.F`, the file this whole artifact was reimplemented from, uses
`BITS_TOTAL=15`: one flag bit and fourteen payload bits, over a state range of 1e-3 to 1e10. The
variant that actually ran, in every case that was actually run, uses
`BITS_TOTAL=16`: **seven checksum bits and nine payload bits**, over a range of 1 to 3e4.

So the checksum was already there, in the deployed code, and this file had just declared it
impractical. Two things were wrong in that paragraph. It does not only catch some cases: it catches
every single bit disturbance tried, 200,000 of them, because the avalanche step spreads a one bit
change across the seven bits of the checksum. And the cost is not vague: it is exactly one more bit
of carrier and five bits of state, which is a number, not a worry.

What the checksum buys, measured side by side against the flag design in test 13:

| | Flag | Checksum |
|---|---|---|
| Carrier never packed, read as packed | 0.4995 | 0.0078 |
| Single bit disturbance above the field, caught | 0 of 20,000 | 200,000 of 200,000 |
| States | 16,384 | 512 |

The flag design cannot catch the disturbance at all, and that is not bad luck. A disturbance above
the field leaves bit 14 exactly where it was, so there is nothing for the flag to notice. The test
asserts the zero rather than describing it, because a reader should be able to see that the
comparison is not rigged.

One thing the flag does better, found by writing the test and getting a failing assertion: stripping
a flagged carrier guarantees the result reads as unpacked, because clearing bit 14 is exactly the
question. Stripping a checksummed carrier writes zero into the field, and about one carrier in 128
has a checksum of zero over its own base bits, so its stripped form reads as carrying payload zero.
Same 1 in 128, pointing the other way. It costs nothing in practice, since nothing strips a carrier
and then asks whether it still has a payload, and it is now asserted as a rate rather than swept up.

Both designs are in the header, the flag one at namespace scope and the checksum one in
`namespace guarded`. Keeping both is not indecision. The pair is the argument: the second design is
the first one after the failure modes were understood, and the price is stated.

The lesson for this file is the older one, that when something looks thin the primary source is
worth checking before concluding it is thin. The repo was built from one Fortran file without
asking whether it was the current one. It was not.

### The argument for the checksum was in the deployment all along

The deployment notes record that a grain's first impact draws its state fresh
and does not decode it, because there is nothing yet to decode.

That is the whole argument, and it is much stronger than the tamper case that prompted the checksum
here. Tampering is a thing that might happen in somebody else's solver. An unpacked first impact
happens to every particle in every run, without fail. With a flag bit, about half of those first
reads return a payload that was never written, assembled out of the grain's own diameter, and hand
it to the model as a damage state. Nothing downstream can tell.

So the checksum is not defensive programming against an unlikely event. It is the thing that makes
the first impact of every grain correct. The README now leads the comparison with that rather than
with the tamper case.

### Wire compatibility, which is a real risk and now has a test

The encoding is not private to the code that writes it. The run writes packed diameters into an
export and a post-processor decodes them later, in Python, written separately from the Fortran. Two
implementations of the same bit layout, and if they ever disagree about one bit then every state
read back afterwards is wrong, silently, with no symptom except physics that does not quite make
sense.

The Python decoder was checked against this C++ line by line: same field positions, same mask, same
two shifted exclusive ors, same seven bit comparison. They agree.

That agreement is now pinned by known answer vectors in test 13, generated by a third
implementation written in JavaScript so that the vectors do not come from the code they check. A
refactor that changes the wire format has to fail that test to get through, which is the only way
this stays safe over time.

### Fast maths, now checked rather than expected

The whole suite passes under `/fp:fast`, all twelve tests, including the non-finite guards and the
assertion that a NaN does not compare equal to itself. It is a build option rather than a one off
experiment, so the claim stays checkable: `-DSTATE_CHANNEL_FAST_MATH=ON`.

The reason it survives is the reason it was expected to: every guard classifies the integer bits and
never compares the float, so there is no floating point comparison for the optimiser to reason
about. Writing the guards as `d > 0.0` and `std::isnan(d)` would have been the natural way and would
have put the whole thing at the mercy of a compiler flag set somewhere else in a build nobody here
controls. The production version does use `.NOT.(d > 0.0)` for the sign and zero case, which is a
small difference from this one worth remembering.

One caveat that MSVC does not expose: on GCC and Clang, `-ffast-math` links a startup object that
sets flush-to-zero and denormals-are-zero. That cannot change what these guards decide, since
`memcpy` sees the bit pattern whatever the FPU mode is, but it changes what the host hands over. A
denormal that would have arrived as a denormal may arrive as zero. Both are refused, so the outcome
is the same, and it is noted only so nobody rediscovers it as a mystery.

### The three placeholders, resolved by deleting the question

The README carried three `⟦value⟧` markers in the sentence arguing that 0.24 micrometres is below
anything downstream: the size distribution bin width, the mesh cell size and the sieve tolerance.

None of the three could be filled honestly. The production routine is a particle user routine and
has no mesh access, so the cell size is not recoverable from it. The binning it does perform is six
equal mass fragment bins, which is breakage model structure rather than an inlet size distribution,
and is the wrong quantity as well as being closer to sponsor material than a public repo should go.
Inventing plausible numbers was the other option and is not an option.

So the sentence was rewritten to state the test rather than the answer: three conditions, all of
which are properties of the case and not of the code, and an instruction to run them against your
own case before adopting the technique. That is more useful than three numbers from somebody else's
compressor, and it is the form the argument should have had from the start. The specific numbers can
be dropped back in later if there is ever a reason to publish them.

One of the three was later filled with a measurement rather than left as a criterion. See below.

### The mesh number, measured instead of asserted

The second bullet of the three part test, that the quantisation is finer than the shortest length
the mesh resolves, is now a number.

The solver output does not print an edge length. It reports orthogonality angle, expansion factor
and aspect ratio, none of which is a length. What it does have is a surface mesh export carrying a
nodal area per node, and the square root of that area is a fair characteristic length for the facet
a node sits on.

Done first on one rotor blade, 34,764 nodes, which gave a finest facet of 2.5 micrometres and a
factor of 10 over the quantisation. Then done properly, over all eleven blade surfaces in the
machine, 385,170 nodes: finest facet 1.09 micrometres, first percentile 8.4, median 165.

The whole machine number is the one to use, and it is the less flattering one. Against the 0.24
micrometres the channel costs a 100 micrometre grain, the typical facet clears it by a factor of 692
and the single finest facet anywhere clears it by 4.6. Quoting the one blade figure would have
overstated the worst case by more than a factor of two, which is exactly the mistake the earlier
micrometre error in this file was: measuring the convenient sample rather than the relevant one.

The factor of five at the extreme is worth remembering rather than rounding off. Two more payload
bits would spend it entirely, so the current width is not as far from the edge as the median
suggests.

Worth being precise about what this is. Nodal area is the area attributed to a node, so its square
root is a characteristic facet length rather than a cell edge, and it is a surface measure rather
than a volume one. It is the right comparison anyway, because the surface is where impacts are
resolved and where the diameter is read.

The geometry is a published multi-stage compressor case rather than proprietary, which is why the
number can be here at all.

### The other compilers, and what they found

Six configurations in CI: gcc, clang and MSVC, each strict and with fast maths. The first run failed
four of the six, and every failure was worth having.

**A real bug, caught by clang on the strict build.** `bench_channel.cpp` disabled vectorisation with
`#pragma clang loop vectorize(off)`. Clang's grammar is `enable`, `disable` or `assume_safety`, and
`off` is not one of them, so it is a hard error rather than an ignored pragma. MSVC never saw it,
because the MSVC branch of that macro is a different spelling entirely. A conditional compiled on one
compiler is a conditional that has not been compiled.

**Fast maths broke four assertions, none of them in the library.** On gcc, `test_guards_non_finite`
failed at `std::isnan(nan_value)`, at `!(nan_value == nan_value)` and at `std::isinf(inf_value)`. On
MSVC only the NaN self comparison failed, because `/fp:fast` is less aggressive than `-ffast-math`.
And `test_guards_sign` failed on gcc at `(bits & sign_mask) != 0`, which took a moment: the test held
its negative values in a `const float` array, one of them `-0.0f`, and `-ffast-math` implies
`-fno-signed-zeros`, so the compiler is entitled to store it as `+0.0f`. The sign the test was about
had gone before the test ran.

In every case the corresponding claim about the library passed. `admissible` refused all of them,
because it reads the integer bits from `memcpy` and never touches the float.

That is the whole argument for writing the guards this way, and it now has evidence rather than
reasoning behind it: **the same flag that deleted those assertions would have deleted guards written
as `d > 0.0` and `std::isnan(d)`.** The natural way to write them is the way that quietly stops
working when somebody sets a flag in a build you do not control.

So the fix is not to weaken the tests. The assertions about IEEE semantics are compiled out under
fast maths, where they are undefined, and the assertions about the library are not, and the sign
test now holds bit patterns instead of float literals. Clang flags the underlying issue itself with
`-Wnan-infinity-disabled`, which is a good warning and worth knowing about.

The README claim that the suite passed under fast maths was true only on MSVC, and only because
MSVC's version of the flag is milder. It has been rewritten to say what is actually true, which is
more interesting than what it used to say.

### A performance assertion is not a test

The second CI run got gcc and MSVC green in both configurations and left clang failing on one thing:
`CHECK(ctx, added_ns > 0.0)` in the benchmark, commented at the time as *it is not free, and a zero
here means a bug*.

It was not a bug. On clang, disabling vectorisation on the baseline loop costs more than the channel
adds, so the difference comes out negative. The channel was fine. The assertion was wrong, and it was
wrong in a way worth writing down: it asserted a **performance ordering**, which depends on the
optimiser, the flags and a shared machine, none of which are properties of the code under test.

Removed, and the reasoning left in the file where the assertion used to be. The sanity checks stay,
because they catch the benchmark measuring nothing at all, which would otherwise be reported as an
excellent result. Everything else the benchmark produces is reported rather than asserted, which is
what should have been true of this one from the start.

Three compilers found three different things: a pragma clang alone rejects, a set of IEEE assumptions
only gcc's fast maths is aggressive enough to break, and a performance assumption only clang's
optimiser disproves. One compiler would have found none of them.

### Still open

Strict aliasing. `test_type_punning` is the test whose subject is undefined behaviour, so it is the
one most likely to find something, and MSVC is the least aggressive of the three about it. It now
passes on gcc as well, which is the compiler most likely to act on a violation, so the `memcpy`
discipline is holding on at least one optimiser that would punish getting it wrong. Clang had not
reached it when the first run died in the benchmark; the next run answers that.

Nothing here has been built on anything but x86-64. The layout assumes IEEE-754 binary32, which is
asserted at compile time, but a big-endian target would want the assumption written down rather than
inferred from the fact that nobody has tried one.
