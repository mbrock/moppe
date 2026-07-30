const std = @import("std");
const specification = @import("specification");
const quantity = @import("quantity");

pub const QuantitySpec = specification.QuantitySpec;

pub const DomainSpec = struct {
    coordinate: QuantitySpec,
};

pub fn Field(
    comptime domain: DomainSpec,
    comptime spec: QuantitySpec,
    comptime sample_count: usize,
    comptime Representation: type,
) type {
    return struct {
        pub const domain_spec = domain;
        pub const quantity_spec = spec;
        pub const count = sample_count;
        pub const representation = Representation;

        values: [sample_count]Representation,
    };
}

fn LaplacianResult(comptime Input: type) type {
    return Field(
        Input.domain_spec,
        specification.laplacianSpec(
            Input.quantity_spec,
            Input.domain_spec.coordinate,
        ),
        Input.count,
        Input.representation,
    );
}

pub fn laplacianPeriodic(
    input: anytype,
    spacing: quantity.Quantity(
        @TypeOf(input).domain_spec.coordinate,
        @TypeOf(input).representation,
    ),
) LaplacianResult(@TypeOf(input)) {
    const Input = @TypeOf(input);
    const Result = LaplacianResult(Input);
    const denominator =
        spacing.coherent_value * spacing.coherent_value;
    var result: Result = undefined;

    for (0..Input.count) |index| {
        const previous = if (index == 0) Input.count - 1 else index - 1;
        const next = if (index + 1 == Input.count) 0 else index + 1;
        result.values[index] =
            (input.values[previous] -
                2.0 * input.values[index] +
                input.values[next]) /
            denominator;
    }
    return result;
}

pub fn interpret(
    comptime named_spec: QuantitySpec,
    derived: anytype,
) Field(
    @TypeOf(derived).domain_spec,
    named_spec,
    @TypeOf(derived).count,
    @TypeOf(derived).representation,
) {
    const Derived = @TypeOf(derived);
    if (comptime !specification.canInterpretAs(
        Derived.quantity_spec,
        named_spec,
    )) {
        @compileError(
            "named interpretation disagrees with derived dimensions or order",
        );
    }

    return .{ .values = derived.values };
}

test "Laplacian derives inverse-length and interpretation names it" {
    const domain: DomainSpec = .{
        .coordinate = specification.specs.spatial_coordinate,
    };
    const ElevationField = Field(
        domain,
        specification.specs.surface_elevation,
        4,
        f64,
    );
    const Coordinate = quantity.Quantity(
        specification.specs.spatial_coordinate,
        f64,
    );
    const elevations: ElevationField = .{
        .values = .{ 0.0, 1.0, 0.0, -1.0 },
    };

    const mechanical = laplacianPeriodic(
        elevations,
        Coordinate.from(2.0, quantity.units.metre),
    );
    const curvature = interpret(
        specification.specs.terrain_curvature,
        mechanical,
    );

    try std.testing.expectEqual(
        .{ 0.0, -0.5, 0.0, 0.5 },
        curvature.values,
    );
    try std.testing.expectEqual(
        specification.specs.terrain_curvature,
        @TypeOf(curvature).quantity_spec,
    );
    try std.testing.expect(
        @TypeOf(mechanical).quantity_spec.meaning !=
            @TypeOf(curvature).quantity_spec.meaning,
    );
}
