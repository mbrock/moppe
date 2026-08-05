+++
id = "TER-023"
title = "Erode standing-water outlets conservatively"
rfc = "RFC-0005"
track = "terrain-landforms"
status = "dropped"
depends_on = ["TER-022"]
order = 75
areas = ["terrain", "lakes", "sediment"]
+++
# Erode standing-water outlets conservatively

## Outcome

A lake can lower its designated dry spill cell under concentrated overflow,
while its submerged outlet and interior remain depositional and every
detached solid enters the existing transport ledger.

## Acceptance

- Only the census-designated dry spill cell bypasses channel-head suppression.
- Spillway cover is entrained before bedrock and all detached volume is routed.
- A synthetic filled basin can lower its outlet over later steps without
  cutting arbitrary channels across the submerged lake bed.
- The renewed physical channel matrix no longer turns suppressed headwater
  incision primarily into perched water and sheer spillway walls.

## Rejected experiment

Giving each dry spill cell full channel share is locally correct and passes a
synthetic conservation test, but it does not address the world-scale failure.
At 25 m2 it reduces inland water only from 3.097 to 3.065 km2, leaves the
90th-percentile slope at 49.7 degrees, and still loses the confluence target.
The implementation was removed rather than retaining an ineffective special
case. TER-024 addresses diffuse runoff across the whole below-channel domain.
