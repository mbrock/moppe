+++
id = "LIV-021"
title = "Distinguish a lip from a cascade in the longitudinal profile"
rfc = "RFC-0003"
track = "living-world"
status = "backlog"
depends_on = ["LIV-020"]
order = 130
areas = ["hydrology", "water", "design"]
+++
# Distinguish a lip from a cascade in the longitudinal profile

## Outcome

Fall candidates carry a named, inspectable classification decided by the
channel's own long profile, not by a shader constant.

## Scope

Moppe clusters steep visible-channel steps into fall candidates with a drop
and a slope, which is enough to intensify foam and not enough to know whether
anything falls. The distinction belongs upstream of rendering, in the profile:
where slope exceeds a threshold, insert basins and level the profile between
them, with basin density proportional to slope. A lip is then the boundary
between two levelled segments; a cascade is a stretch that stays steep without
producing one.

That threshold is a fiat decision about what counts as a waterfall in this
world, exactly as the persistence threshold is a decision about what counts as
a ridge. It belongs in recipe data and in TRACE, not in a constant. The
project's own hydrology note argues this at length: water bodies have crisp
token boundaries and a messy kind taxonomy, and every remaining water
rendering problem is a kind-assignment problem.

## Acceptance

- FALLS distinguishes lips from cascades and reports the threshold used.
- The classification is deterministic and survives a profile change.
- The threshold is editable program data.

## Research

*Procedural Riverscapes* (Sheaf `#AK7NGE`): monotone-profile refinement,
cascade insertion, basins with slope-proportional density, rock obstacles at
basin intersections. Sheaf notes `#AHZGJ5` and `#2KBHCL` for why the threshold
is a granularity policy rather than a measurement.
