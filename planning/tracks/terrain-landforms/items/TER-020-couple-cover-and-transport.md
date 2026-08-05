+++
id = "TER-020"
title = "Couple mobile cover, bedrock incision, and transport capacity"
rfc = "RFC-0005"
track = "terrain-landforms"
status = "done"
depends_on = ["TER-010"]
order = 40
areas = ["terrain", "sediment", "hydrology"]
+++
# Couple mobile cover, bedrock incision, and transport capacity

## Outcome

Deposited sediment inhibits bedrock incision, is re-entrained before bedrock,
and moves under a capacity law expressed from discharge, slope, and channel
scale rather than potential detachment alone.

## Acceptance

- Thick cover protects bedrock in a synthetic channel.
- A capacity increase entrains cover before cutting rock.
- A capacity decrease aggrades while carrying conserved excess onward.
- Parameter changes preserve physical units and time-step interpretation.

## Evidence

The router consumes spare capacity in the required order: incoming load,
stored cover, then potential bedrock detachment. Synthetic tests prove that a
thick covered channel entrains cover without reporting bedrock incision, that
additional capacity reaches bedrock only after cover is exhausted, and that
aggradation and downstream export close the solid-volume ledger.

Capacity is the typed product of duration, contributing area, runoff,
effective sediment concentration, slope, and channel share. Its direct test
proves the expected cubic-metre result and linear time scaling. The selected
Play concentration is 0.00002 at unit slope; the decision matrix and rendered
evidence are recorded in `../findings.md`.
