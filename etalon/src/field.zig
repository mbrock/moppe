const std = @import("std");
const specification = @import("specification");
const quantity = @import("quantity");

pub const QuantitySpec = specification.QuantitySpec;

fn SliceChild(comptime Slice: type) type {
    const pointer = switch (@typeInfo(Slice)) {
        .pointer => |info| info,
        else => @compileError("a field column must be a slice"),
    };
    if (pointer.size != .slice) {
        @compileError("a field column must be a slice");
    }
    return pointer.child;
}

fn LaplacianColumnValue(
    comptime InputSlice: type,
    comptime Domain: type,
) type {
    const Input = SliceChild(InputSlice);
    if (!@hasDecl(Input, "quantity_spec") or
        !@hasDecl(Input, "representation"))
    {
        @compileError("a field column must contain quantity values");
    }
    if (Input.quantity_spec.tensor_order != .scalar) {
        @compileError("the scalar stencil does not define a vector Laplacian");
    }
    if (!@hasDecl(Domain, "coordinate_spec") or
        !@hasDecl(Domain, "count") or
        !@hasDecl(Domain, "index") or
        !@hasDecl(Domain, "offset") or
        !@hasDecl(Domain, "previous") or
        !@hasDecl(Domain, "next"))
    {
        @compileError(
            "a periodic field domain needs coordinates and neighbor access",
        );
    }
    return quantity.Quantity(
        specification.laplacianSpec(
            Input.quantity_spec,
            Domain.coordinate_spec,
        ),
        Input.representation,
    );
}

/// Apply the periodic second-difference stencil directly to a typed column.
/// The input may be a MultiArrayList field slice; no row materialization or
/// input copy is required.
pub fn laplacianPeriodicColumnAlloc(
    allocator: std.mem.Allocator,
    domain: anytype,
    input: anytype,
    spacing: anytype,
) ![]LaplacianColumnValue(@TypeOf(input), @TypeOf(domain)) {
    const Input = SliceChild(@TypeOf(input));
    const Domain = @TypeOf(domain);
    const Spacing = @TypeOf(spacing);
    if (comptime Input.representation != Spacing.representation) {
        @compileError("field and spacing representations disagree");
    }
    if (comptime !Spacing.quantity_spec.eql(Domain.coordinate_spec)) {
        @compileError("spacing does not measure the domain coordinate");
    }
    if (input.len == 0) return error.EmptyField;
    if (input.len != domain.count()) return error.DomainSizeMismatch;
    if (spacing.coherent_value == 0) return error.ZeroSpacing;

    const Result = LaplacianColumnValue(
        @TypeOf(input),
        Domain,
    );
    const denominator =
        spacing.coherent_value * spacing.coherent_value;
    const result = try allocator.alloc(Result, input.len);
    errdefer allocator.free(result);

    for (result, 0..) |*output, index| {
        const site = domain.index(index).?;
        const previous = domain.offset(domain.previous(site));
        const next = domain.offset(domain.next(site));
        output.* = .{
            .coherent_value = (input[previous].coherent_value -
                2.0 * input[index].coherent_value +
                input[next].coherent_value) /
                denominator,
        };
    }
    return result;
}

pub fn interpretValue(
    comptime named_spec: QuantitySpec,
    derived: anytype,
) quantity.Quantity(named_spec, @TypeOf(derived).representation) {
    const Derived = @TypeOf(derived);
    if (comptime !specification.canInterpretAs(
        Derived.quantity_spec,
        named_spec,
    )) {
        @compileError(
            "named interpretation disagrees with derived dimensions or order",
        );
    }
    return .{ .coherent_value = derived.coherent_value };
}

test "a typed column can enter the field calculus without row copies" {
    const TestDomain = struct {
        const Self = @This();

        pub const coordinate_spec =
            specification.specs.spatial_coordinate;
        pub const Index = struct { offset: usize };

        count_value: usize,

        pub fn count(self: Self) usize {
            return self.count_value;
        }

        pub fn index(self: Self, requested: usize) ?Index {
            if (requested >= self.count_value) return null;
            return .{ .offset = requested };
        }

        pub fn offset(_: Self, site: Index) usize {
            return site.offset;
        }

        pub fn previous(self: Self, site: Index) Index {
            return .{
                .offset = if (site.offset == 0)
                    self.count_value - 1
                else
                    site.offset - 1,
            };
        }

        pub fn next(self: Self, site: Index) Index {
            return .{
                .offset = if (site.offset + 1 == self.count_value)
                    0
                else
                    site.offset + 1,
            };
        }
    };
    const Elevation = quantity.QuantityPoint(
        specification.specs.surface_elevation,
        specification.specs.vertical_displacement,
        f64,
    );
    const Coordinate = quantity.Quantity(
        specification.specs.spatial_coordinate,
        f64,
    );
    const elevations = [_]Elevation{
        Elevation.from(0.0, quantity.units.metre),
        Elevation.from(1.0, quantity.units.metre),
        Elevation.from(0.0, quantity.units.metre),
        Elevation.from(-1.0, quantity.units.metre),
    };
    const elevation_slice: []const Elevation = &elevations;

    const mechanical = try laplacianPeriodicColumnAlloc(
        std.testing.allocator,
        TestDomain{ .count_value = elevations.len },
        elevation_slice,
        Coordinate.from(2.0, quantity.units.metre),
    );
    defer std.testing.allocator.free(mechanical);

    try std.testing.expectEqual(4, mechanical.len);
    try std.testing.expectApproxEqAbs(
        -0.5,
        mechanical[1].coherent_value,
        1e-12,
    );
    const curvature = interpretValue(
        specification.specs.terrain_curvature,
        mechanical[1],
    );
    try std.testing.expectEqual(
        specification.specs.terrain_curvature,
        @TypeOf(curvature).quantity_spec,
    );
}
