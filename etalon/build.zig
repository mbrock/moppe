const std = @import("std");

pub fn build(b: *std.Build) void {
    const target = b.standardTargetOptions(.{});
    const optimize = b.standardOptimizeOption(.{});

    const dimension = b.createModule(.{
        .root_source_file = b.path("src/dimension.zig"),
        .target = target,
    });
    const specification = b.createModule(.{
        .root_source_file = b.path("src/specification.zig"),
        .target = target,
        .imports = &.{
            .{ .name = "dimension", .module = dimension },
        },
    });
    const quantity = b.createModule(.{
        .root_source_file = b.path("src/quantity.zig"),
        .target = target,
        .imports = &.{
            .{ .name = "specification", .module = specification },
        },
    });
    const field = b.createModule(.{
        .root_source_file = b.path("src/field.zig"),
        .target = target,
        .imports = &.{
            .{ .name = "specification", .module = specification },
            .{ .name = "quantity", .module = quantity },
        },
    });
    const etalon = b.addModule("etalon", .{
        .root_source_file = b.path("src/root.zig"),
        .target = target,
        .imports = &.{
            .{ .name = "dimension", .module = dimension },
            .{ .name = "specification", .module = specification },
            .{ .name = "quantity", .module = quantity },
            .{ .name = "field", .module = field },
        },
    });

    const executable = b.addExecutable(.{
        .name = "etalon",
        .root_module = b.createModule(.{
            .root_source_file = b.path("src/main.zig"),
            .target = target,
            .optimize = optimize,
            .imports = &.{
                .{ .name = "etalon", .module = etalon },
            },
        }),
    });
    b.installArtifact(executable);

    const run_command = b.addRunArtifact(executable);
    run_command.step.dependOn(b.getInstallStep());
    const run_step = b.step("run", "Run the quantity experiment");
    run_step.dependOn(&run_command.step);

    const unit_step = b.step("unit", "Run valid quantity programs");
    for ([_]*std.Build.Module{
        dimension,
        specification,
        quantity,
        field,
        etalon,
    }) |module| {
        const tests = b.addTest(.{ .root_module = module });
        const run_tests = b.addRunArtifact(tests);
        unit_step.dependOn(&run_tests.step);
    }

    const test_step = b.step(
        "test",
        "Run valid programs and check rejected programs",
    );
    test_step.dependOn(unit_step);

    const rejected_addition = b.addTest(.{
        .root_module = b.createModule(.{
            .root_source_file = b.path("examples/rejected_addition.zig"),
            .target = target,
            .imports = &.{
                .{ .name = "etalon", .module = etalon },
            },
        }),
    });
    rejected_addition.expect_errors = .{
        .contains = "cannot add quantities with different specifications",
    };
    test_step.dependOn(&rejected_addition.step);

    const rejected_interpretation = b.addTest(.{
        .root_module = b.createModule(.{
            .root_source_file = b.path(
                "examples/rejected_interpretation.zig",
            ),
            .target = target,
            .imports = &.{
                .{ .name = "etalon", .module = etalon },
            },
        }),
    });
    rejected_interpretation.expect_errors = .{
        .contains = "named interpretation disagrees with derived dimensions or order",
    };
    test_step.dependOn(&rejected_interpretation.step);
}
