const std = @import("std");
const specification = @import("specification");
const quantity = @import("quantity");

pub const QuantitySpec = specification.QuantitySpec;

fn isQuantityValue(comptime Value: type) bool {
    return switch (@typeInfo(Value)) {
        .@"struct" => @hasDecl(Value, "quantity_spec") and
            @hasDecl(Value, "representation"),
        else => false,
    };
}

fn validateRow(comptime Row: type) void {
    const row = switch (@typeInfo(Row)) {
        .@"struct" => |info| info,
        else => @compileError("Bundle rows must be structs"),
    };
    inline for (row.field_names, row.field_types) |field_name, FieldType| {
        if (!isQuantityValue(FieldType)) {
            @compileError(std.fmt.comptimePrint(
                "Bundle row field '{s}' is not a quantity value",
                .{field_name},
            ));
        }
    }
}

fn validateDomain(comptime Domain: type) void {
    if (!@hasDecl(Domain, "Index") or
        !@hasDecl(Domain, "count") or
        !@hasDecl(Domain, "offset"))
    {
        @compileError(
            "Bundle domains need Index, count(), and offset()",
        );
    }
}

/// A finite bundle whose row struct is its schema. MultiArrayList stores one
/// contiguous array for each row field while retaining the field's quantity
/// type in the column API.
/// The owned Slice freezes cardinality after construction.
pub fn Bundle(comptime Domain: type, comptime Row: type) type {
    validateDomain(Domain);
    validateRow(Row);

    const Storage = std.MultiArrayList(Row);
    return struct {
        const Self = @This();

        pub const Column = Storage.Field;

        domain: Domain,
        columns: Storage.Slice,

        pub fn init(
            allocator: std.mem.Allocator,
            domain: Domain,
            initial_rows: []const Row,
        ) !Self {
            if (initial_rows.len != domain.count()) {
                return error.DomainSizeMismatch;
            }

            var storage = try Storage.initCapacity(
                allocator,
                initial_rows.len,
            );
            errdefer storage.deinit(allocator);
            for (initial_rows) |initial_row| {
                storage.appendAssumeCapacity(initial_row);
            }
            return .{
                .domain = domain,
                .columns = storage.toOwnedSlice(),
            };
        }

        pub fn deinit(self: *Self, allocator: std.mem.Allocator) void {
            self.columns.deinit(allocator);
            self.* = undefined;
        }

        pub fn count(self: Self) usize {
            return self.columns.len;
        }

        pub fn row(self: Self, index: Domain.Index) Row {
            return self.columns.get(self.domain.offset(index));
        }

        pub fn setRow(
            self: *Self,
            index: Domain.Index,
            value: Row,
        ) void {
            self.columns.set(self.domain.offset(index), value);
        }

        pub fn ColumnType(comptime column_name: Column) type {
            return @FieldType(Row, @tagName(column_name));
        }

        pub fn column(
            self: *Self,
            comptime column_name: Column,
        ) []ColumnType(column_name) {
            return self.columns.items(column_name);
        }

        pub fn columnConst(
            self: *const Self,
            comptime column_name: Column,
        ) []const ColumnType(column_name) {
            return self.columns.items(column_name);
        }

        pub fn columnSpec(
            comptime column_name: Column,
        ) QuantitySpec {
            return ColumnType(column_name).quantity_spec;
        }
    };
}

/// The smallest domain useful for the current calculus experiment. Index
/// construction is checked once; Bundle row access then uses the domain's
/// explicit index type instead of an unowned usize.
pub fn PeriodicLineDomain(comptime coordinate: QuantitySpec) type {
    return struct {
        const Self = @This();

        pub const coordinate_spec = coordinate;
        pub const Index = struct {
            offset: usize,
        };

        sample_count: usize,

        pub fn init(sample_count: usize) Self {
            return .{ .sample_count = sample_count };
        }

        pub fn count(self: Self) usize {
            return self.sample_count;
        }

        pub fn index(self: Self, requested_offset: usize) ?Index {
            if (requested_offset >= self.sample_count) return null;
            return .{ .offset = requested_offset };
        }

        pub fn offset(self: Self, index_value: Index) usize {
            std.debug.assert(index_value.offset < self.sample_count);
            return index_value.offset;
        }

        pub fn previous(self: Self, index_value: Index) Index {
            return .{
                .offset = if (index_value.offset == 0)
                    self.sample_count - 1
                else
                    index_value.offset - 1,
            };
        }

        pub fn next(self: Self, index_value: Index) Index {
            return .{
                .offset = if (index_value.offset + 1 == self.sample_count)
                    0
                else
                    index_value.offset + 1,
            };
        }
    };
}

test "MultiArrayList gives a finite Bundle typed contiguous columns" {
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
    const SurfaceRow = struct {
        elevation: Elevation,
        water_depth: WaterDepth,
    };
    const Domain = PeriodicLineDomain(specs.spatial_coordinate);
    const Surface = Bundle(Domain, SurfaceRow);
    const domain = Domain.init(3);
    const initial = [_]SurfaceRow{
        .{
            .elevation = Elevation.from(4.0, units.metre),
            .water_depth = WaterDepth.from(0.0, units.metre),
        },
        .{
            .elevation = Elevation.from(5.0, units.metre),
            .water_depth = WaterDepth.from(0.5, units.metre),
        },
        .{
            .elevation = Elevation.from(6.0, units.metre),
            .water_depth = WaterDepth.from(1.0, units.metre),
        },
    };
    var surface = try Surface.init(std.testing.allocator, domain, &initial);
    defer surface.deinit(std.testing.allocator);

    try std.testing.expectEqual(3, surface.count());
    try std.testing.expectEqual(
        specs.surface_elevation,
        Surface.columnSpec(.elevation),
    );
    try std.testing.expectEqual(
        specs.standing_water_depth,
        Surface.columnSpec(.water_depth),
    );
    try std.testing.expectEqual(
        @sizeOf(f64),
        @sizeOf(Surface.ColumnType(.elevation)),
    );

    const elevations = surface.column(.elevation);
    const depths = surface.column(.water_depth);
    try std.testing.expect(@TypeOf(elevations) == []Elevation);
    try std.testing.expect(@TypeOf(depths) == []WaterDepth);
    try std.testing.expectEqual(3, elevations.len);
    try std.testing.expectEqual(3, depths.len);
    try std.testing.expect(
        @intFromPtr(elevations.ptr) != @intFromPtr(depths.ptr),
    );

    depths[1] = WaterDepth.from(0.75, units.metre);
    const middle = surface.row(domain.index(1).?);
    try std.testing.expectApproxEqAbs(
        0.75,
        middle.water_depth.inUnit(units.metre),
        1e-12,
    );
}

test "Bundle construction enforces domain cardinality" {
    const Elevation = quantity.QuantityPoint(
        specification.specs.surface_elevation,
        specification.specs.vertical_displacement,
        f64,
    );
    const Row = struct { elevation: Elevation };
    const Domain = PeriodicLineDomain(
        specification.specs.spatial_coordinate,
    );
    const Surface = Bundle(Domain, Row);
    const rows = [_]Row{};

    try std.testing.expectError(
        error.DomainSizeMismatch,
        Surface.init(std.testing.allocator, Domain.init(1), &rows),
    );
}
