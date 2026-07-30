//! The public surface of the Étalon quantity experiment.

pub const dimension = @import("dimension");
pub const specification = @import("specification");
pub const quantity = @import("quantity");
pub const field = @import("field");
pub const bundle = @import("bundle");

test {
    _ = dimension;
    _ = specification;
    _ = quantity;
    _ = field;
    _ = bundle;
}

test "a Bundle column flows directly into the field calculus" {
    const std = @import("std");
    const specs = specification.specs;
    const units = quantity.units;
    const Elevation = quantity.QuantityPoint(
        specs.surface_elevation,
        specs.vertical_displacement,
        f64,
    );
    const WaterDepth = quantity.Quantity(
        specs.standing_water_depth,
        f64,
    );
    const Curvature = quantity.Quantity(specs.terrain_curvature, f64);
    const SurfaceRow = struct {
        elevation: Elevation,
        water_depth: WaterDepth,
        curvature: Curvature,
    };
    const Domain = bundle.PeriodicLineDomain(specs.spatial_coordinate);
    const Surface = bundle.Bundle(Domain, SurfaceRow);
    const domain = Domain.init(4);
    const zero_depth = WaterDepth.from(0.0, units.metre);
    const zero_curvature: Curvature = .{ .coherent_value = 0.0 };
    const initial = [_]SurfaceRow{
        .{
            .elevation = Elevation.from(0.0, units.metre),
            .water_depth = zero_depth,
            .curvature = zero_curvature,
        },
        .{
            .elevation = Elevation.from(1.0, units.metre),
            .water_depth = zero_depth,
            .curvature = zero_curvature,
        },
        .{
            .elevation = Elevation.from(0.0, units.metre),
            .water_depth = zero_depth,
            .curvature = zero_curvature,
        },
        .{
            .elevation = Elevation.from(-1.0, units.metre),
            .water_depth = zero_depth,
            .curvature = zero_curvature,
        },
    };
    var surface = try Surface.init(
        std.testing.allocator,
        domain,
        &initial,
    );
    defer surface.deinit(std.testing.allocator);

    const Coordinate = quantity.Quantity(specs.spatial_coordinate, f64);
    const mechanical = try field.laplacianPeriodicColumnAlloc(
        std.testing.allocator,
        surface.domain,
        surface.columnConst(.elevation),
        Coordinate.from(2.0, units.metre),
    );
    defer std.testing.allocator.free(mechanical);

    for (surface.column(.curvature), mechanical) |*target, derived| {
        target.* = field.interpretValue(specs.terrain_curvature, derived);
    }

    try std.testing.expectEqual(
        [_]f64{ 0.0, -0.5, 0.0, 0.5 },
        .{
            surface.columnConst(.curvature)[0].coherent_value,
            surface.columnConst(.curvature)[1].coherent_value,
            surface.columnConst(.curvature)[2].coherent_value,
            surface.columnConst(.curvature)[3].coherent_value,
        },
    );
}
