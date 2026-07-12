#include <moppe/terrain/cpu_evaluator.hh>
#include <moppe/terrain/noise.hh>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <functional>
#include <limits>
#include <memory>
#include <stdexcept>
#include <thread>
#include <type_traits>
#include <unordered_map>
#include <variant>

namespace moppe::terrain {
  namespace {
    class PerlinTable {
    public:
      explicit PerlinTable (std::uint32_t seed)
          : m_permutation (make_perlin_permutation (seed)) {}

      float noise (float x, float y) const {
        return noise (x, y, 256, 256);
      }

      float noise (float x, float y, int period_x, int period_y) const {
        const float floor_x = std::floor (x);
        const float floor_y = std::floor (y);
        const int xi = wrap_lattice (static_cast<int> (floor_x), period_x);
        const int yi = wrap_lattice (static_cast<int> (floor_y), period_y);
        const int xj = wrap_lattice (static_cast<int> (floor_x) + 1, period_x);
        const int yj = wrap_lattice (static_cast<int> (floor_y) + 1, period_y);
        const float xf = x - floor_x;
        const float yf = y - floor_y;
        const float u = fade (xf);
        const float v = fade (yf);

        const int aa = m_permutation[m_permutation[xi] + yi];
        const int ab = m_permutation[m_permutation[xi] + yj];
        const int ba = m_permutation[m_permutation[xj] + yi];
        const int bb = m_permutation[m_permutation[xj] + yj];

        return lerp (lerp (grad (aa, xf, yf), grad (ba, xf - 1, yf), u),
                     lerp (grad (ab, xf, yf - 1), grad (bb, xf - 1, yf - 1), u),
                     v);
      }

      float
      fbm (float x, float y, int octaves, float lacunarity, float gain) const {
        float sum = 0, amplitude = 1, frequency = 1, norm = 0;
        for (int i = 0; i < octaves; ++i) {
          sum += amplitude * noise (x * frequency, y * frequency);
          norm += amplitude;
          amplitude *= gain;
          frequency *= lacunarity;
        }
        return sum / norm;
      }

      float ridged (
        float x, float y, int octaves, float lacunarity, float gain) const {
        float sum = 0, amplitude = 0.5f, frequency = 1;
        float weight = 1, norm = 0;
        for (int i = 0; i < octaves; ++i) {
          float value = 1.0f - std::fabs (noise (x * frequency, y * frequency));
          value *= value;
          value *= weight;
          weight = std::clamp (value * 2.0f, 0.0f, 1.0f);
          sum += value * amplitude;
          norm += amplitude;
          amplitude *= gain;
          frequency *= lacunarity;
        }
        return sum / norm;
      }

      float periodic_fbm (float x,
                          float y,
                          int period_x,
                          int period_y,
                          int octaves,
                          int lacunarity,
                          float gain) const {
        float sum = 0, amplitude = 1, norm = 0;
        int octave_period_x = period_x;
        int octave_period_y = period_y;
        float frequency = 1;
        for (int i = 0; i < octaves; ++i) {
          sum += amplitude * noise (x * frequency,
                                    y * frequency,
                                    octave_period_x,
                                    octave_period_y);
          norm += amplitude;
          amplitude *= gain;
          frequency *= static_cast<float> (lacunarity);
          octave_period_x *= lacunarity;
          octave_period_y *= lacunarity;
        }
        return sum / norm;
      }

      float periodic_ridged (float x,
                             float y,
                             int period_x,
                             int period_y,
                             int octaves,
                             int lacunarity,
                             float gain) const {
        float sum = 0, amplitude = 0.5f, frequency = 1;
        float weight = 1, norm = 0;
        int octave_period_x = period_x;
        int octave_period_y = period_y;
        for (int i = 0; i < octaves; ++i) {
          float value =
            1.0f -
            std::fabs (noise (
              x * frequency, y * frequency, octave_period_x, octave_period_y));
          value *= value;
          value *= weight;
          weight = std::clamp (value * 2.0f, 0.0f, 1.0f);
          sum += value * amplitude;
          norm += amplitude;
          amplitude *= gain;
          frequency *= static_cast<float> (lacunarity);
          octave_period_x *= lacunarity;
          octave_period_y *= lacunarity;
        }
        return sum / norm;
      }

    private:
      static int wrap_lattice (int value, int period) {
        const int wrapped = value % period;
        return (wrapped < 0 ? wrapped + period : wrapped) & 255;
      }

      static float fade (float value) {
        return value * value * value * (value * (value * 6 - 15) + 10);
      }

      static float lerp (float a, float b, float amount) {
        return a + amount * (b - a);
      }

      static float grad (int hash, float x, float y) {
        switch (hash & 7) {
        case 0:
          return x + y;
        case 1:
          return x - y;
        case 2:
          return -x + y;
        case 3:
          return -x - y;
        case 4:
          return x;
        case 5:
          return -x;
        case 6:
          return y;
        default:
          return -y;
        }
      }

      PerlinPermutation m_permutation;
    };

    struct LoadConstant {
      float value;
    };
    struct LoadX {};
    struct LoadY {};
    struct AddRegisters {
      std::size_t left, right;
    };
    struct SubtractRegisters {
      std::size_t left, right;
    };
    struct MultiplyRegisters {
      std::size_t left, right;
    };
    struct MultiplyAddRegisters {
      std::size_t multiplier, multiplicand, addend;
    };
    struct SineRegister {
      std::size_t operand;
    };
    struct SmoothstepRegister {
      float edge0, edge1;
      std::size_t operand;
    };
    struct PerlinRegister {
      std::shared_ptr<const PerlinTable> table;
      std::size_t x, y;
    };
    struct FbmRegister {
      std::shared_ptr<const PerlinTable> table;
      std::size_t x, y;
      int octaves;
      float lacunarity, gain;
    };
    struct RidgedRegister {
      std::shared_ptr<const PerlinTable> table;
      std::size_t x, y;
      int octaves;
      float lacunarity, gain;
    };
    struct PeriodicFbmRegister {
      std::shared_ptr<const PerlinTable> table;
      std::size_t x, y;
      int period_x, period_y, octaves, lacunarity;
      float gain;
    };
    struct PeriodicRidgedRegister {
      std::shared_ptr<const PerlinTable> table;
      std::size_t x, y;
      int period_x, period_y, octaves, lacunarity;
      float gain;
    };

    using Instruction = std::variant<LoadConstant,
                                     LoadX,
                                     LoadY,
                                     AddRegisters,
                                     SubtractRegisters,
                                     MultiplyRegisters,
                                     MultiplyAddRegisters,
                                     SineRegister,
                                     SmoothstepRegister,
                                     PerlinRegister,
                                     FbmRegister,
                                     RidgedRegister,
                                     PeriodicFbmRegister,
                                     PeriodicRidgedRegister>;

    struct Program {
      std::vector<Instruction> instructions;
      std::size_t output;
    };

    Program compile (const ScalarField& field) {
      Program program;
      program.instructions.reserve (unique_node_count (field));
      std::unordered_map<const expression::Node*, std::size_t> registers;
      std::unordered_map<std::uint32_t, std::shared_ptr<const PerlinTable>>
        tables;

      const auto table_for = [&tables] (Seed seed) {
        if (const auto found = tables.find (seed.value); found != tables.end ())
          return found->second;
        auto table = std::make_shared<const PerlinTable> (seed.value);
        tables.emplace (seed.value, table);
        return table;
      };

      std::function<std::size_t (const expression::NodePtr&)> emit;
      emit = [&] (const expression::NodePtr& node) -> std::size_t {
        if (const auto found = registers.find (node.get ());
            found != registers.end ())
          return found->second;

        Instruction instruction = std::visit (
          [&] (const auto& operation) -> Instruction {
            using T = std::decay_t<decltype (operation)>;
            if constexpr (std::is_same_v<T, expression::Constant>)
              return LoadConstant { operation.value };
            else if constexpr (std::is_same_v<T, expression::CoordinateX>)
              return LoadX {};
            else if constexpr (std::is_same_v<T, expression::CoordinateY>)
              return LoadY {};
            else if constexpr (std::is_same_v<T, expression::Add>)
              return AddRegisters { emit (operation.left),
                                    emit (operation.right) };
            else if constexpr (std::is_same_v<T, expression::Subtract>)
              return SubtractRegisters { emit (operation.left),
                                         emit (operation.right) };
            else if constexpr (std::is_same_v<T, expression::Multiply>)
              return MultiplyRegisters { emit (operation.left),
                                         emit (operation.right) };
            else if constexpr (std::is_same_v<T, expression::MultiplyAdd>)
              return MultiplyAddRegisters { emit (operation.multiplier),
                                            emit (operation.multiplicand),
                                            emit (operation.addend) };
            else if constexpr (std::is_same_v<T, expression::Sine>)
              return SineRegister { emit (operation.operand) };
            else if constexpr (std::is_same_v<T, expression::Smoothstep>)
              return SmoothstepRegister { operation.edge0,
                                          operation.edge1,
                                          emit (operation.operand) };
            else if constexpr (std::is_same_v<T, expression::PerlinNoise>)
              return PerlinRegister { table_for (operation.seed),
                                      emit (operation.x),
                                      emit (operation.y) };
            else if constexpr (std::is_same_v<T, expression::FbmNoise>)
              return FbmRegister {
                table_for (operation.seed), emit (operation.x),
                emit (operation.y),         operation.octaves,
                operation.lacunarity,       operation.gain
              };
            else if constexpr (std::is_same_v<T, expression::RidgedNoise>)
              return RidgedRegister {
                table_for (operation.seed), emit (operation.x),
                emit (operation.y),         operation.octaves,
                operation.lacunarity,       operation.gain
              };
            else if constexpr (std::is_same_v<T, expression::PeriodicFbmNoise>)
              return PeriodicFbmRegister {
                table_for (operation.seed), emit (operation.x),
                emit (operation.y),         operation.period_x,
                operation.period_y,         operation.octaves,
                operation.lacunarity,       operation.gain
              };
            else
              return PeriodicRidgedRegister {
                table_for (operation.seed), emit (operation.x),
                emit (operation.y),         operation.period_x,
                operation.period_y,         operation.octaves,
                operation.lacunarity,       operation.gain
              };
          },
          node->operation);

        const std::size_t index = program.instructions.size ();
        program.instructions.push_back (std::move (instruction));
        registers.emplace (node.get (), index);
        return index;
      };

      program.output = emit (field.node ());
      return program;
    }

    void validate (const Domain2D& domain) {
      if (domain.width < 2 || domain.height < 2)
        throw std::invalid_argument (
          "a field domain needs at least two samples per axis");
      if (!(domain.max_x > domain.min_x) || !(domain.max_y > domain.min_y))
        throw std::invalid_argument (
          "a field domain needs increasing coordinate bounds");
      if (domain.width >
          std::numeric_limits<std::size_t>::max () / domain.height)
        throw std::length_error ("field domain is too large");
    }
  }

  ScalarRaster::ScalarRaster (Domain2D domain, std::vector<float> values)
      : m_domain (domain), m_values (std::move (values)) {
    if (m_values.size () != m_domain.width * m_domain.height)
      throw std::invalid_argument (
        "raster value count does not match its domain");
  }

  float ScalarRaster::at (std::size_t x, std::size_t y) const {
    if (x >= m_domain.width || y >= m_domain.height)
      throw std::out_of_range ("raster coordinate is out of range");
    return m_values[y * m_domain.width + x];
  }

  float ScalarRaster::min_value () const {
    return *std::min_element (m_values.begin (), m_values.end ());
  }

  float ScalarRaster::max_value () const {
    return *std::max_element (m_values.begin (), m_values.end ());
  }

  ScalarRaster CpuEvaluator::evaluate (const ScalarField& field,
                                       const Domain2D& domain) const {
    validate (domain);
    const Program program = compile (field);
    std::vector<float> output (domain.width * domain.height);
    const float inv_x = 1.0f / static_cast<float> (domain.width - 1);
    const float inv_y = 1.0f / static_cast<float> (domain.height - 1);

    std::atomic<std::size_t> next_row = 0;
    std::atomic<std::size_t> completed_rows = 0;
    const auto evaluate_rows = [&] {
      std::vector<float> registers (program.instructions.size ());
      for (;;) {
        const std::size_t y = next_row.fetch_add (1, std::memory_order_relaxed);
        if (y >= domain.height)
          break;
        const float fy = static_cast<float> (y) * inv_y;
        const float py = domain.min_y + fy * (domain.max_y - domain.min_y);

        for (std::size_t x = 0; x < domain.width; ++x) {
          const float fx = static_cast<float> (x) * inv_x;
          const float px = domain.min_x + fx * (domain.max_x - domain.min_x);

          for (std::size_t i = 0; i < program.instructions.size (); ++i) {
            registers[i] = std::visit (
              [&] (const auto& instruction) {
                using T = std::decay_t<decltype (instruction)>;
                if constexpr (std::is_same_v<T, LoadConstant>)
                  return instruction.value;
                else if constexpr (std::is_same_v<T, LoadX>)
                  return px;
                else if constexpr (std::is_same_v<T, LoadY>)
                  return py;
                else if constexpr (std::is_same_v<T, AddRegisters>)
                  return registers[instruction.left] +
                         registers[instruction.right];
                else if constexpr (std::is_same_v<T, SubtractRegisters>)
                  return registers[instruction.left] -
                         registers[instruction.right];
                else if constexpr (std::is_same_v<T, MultiplyRegisters>)
                  return registers[instruction.left] *
                         registers[instruction.right];
                else if constexpr (std::is_same_v<T, MultiplyAddRegisters>)
                  return std::fma (registers[instruction.multiplier],
                                   registers[instruction.multiplicand],
                                   registers[instruction.addend]);
                else if constexpr (std::is_same_v<T, SineRegister>)
                  return std::sin (registers[instruction.operand]);
                else if constexpr (std::is_same_v<T, SmoothstepRegister>) {
                  float amount =
                    (registers[instruction.operand] - instruction.edge0) /
                    (instruction.edge1 - instruction.edge0);
                  amount = std::clamp (amount, 0.0f, 1.0f);
                  return amount * amount * (3.0f - 2.0f * amount);
                } else if constexpr (std::is_same_v<T, PerlinRegister>)
                  return instruction.table->noise (registers[instruction.x],
                                                   registers[instruction.y]);
                else if constexpr (std::is_same_v<T, FbmRegister>)
                  return instruction.table->fbm (registers[instruction.x],
                                                 registers[instruction.y],
                                                 instruction.octaves,
                                                 instruction.lacunarity,
                                                 instruction.gain);
                else if constexpr (std::is_same_v<T, RidgedRegister>)
                  return instruction.table->ridged (registers[instruction.x],
                                                    registers[instruction.y],
                                                    instruction.octaves,
                                                    instruction.lacunarity,
                                                    instruction.gain);
                else if constexpr (std::is_same_v<T, PeriodicFbmRegister>)
                  return instruction.table->periodic_fbm (
                    registers[instruction.x],
                    registers[instruction.y],
                    instruction.period_x,
                    instruction.period_y,
                    instruction.octaves,
                    instruction.lacunarity,
                    instruction.gain);
                else
                  return instruction.table->periodic_ridged (
                    registers[instruction.x],
                    registers[instruction.y],
                    instruction.period_x,
                    instruction.period_y,
                    instruction.octaves,
                    instruction.lacunarity,
                    instruction.gain);
              },
              program.instructions[i]);
          }

          output[y * domain.width + x] = registers[program.output];
        }
        const std::size_t completed =
          completed_rows.fetch_add (1, std::memory_order_relaxed) + 1;
        if (m_progress && (completed % 8 == 0 || completed == domain.height))
          m_progress (completed, domain.height);
      }
    };

    const std::size_t hardware_threads =
      std::max (1u, std::thread::hardware_concurrency ());
    // Keep one logical CPU available for the animated loading screen and
    // other main-thread work while large fields are materialized.
    const std::size_t available_threads =
      hardware_threads > 1 ? hardware_threads - 1 : 1;
    const std::size_t worker_count =
      output.size () < 65536 ? 1 : std::min (domain.height, available_threads);
    if (worker_count == 1) {
      evaluate_rows ();
    } else {
      std::vector<std::jthread> workers;
      workers.reserve (worker_count - 1);
      for (std::size_t i = 1; i < worker_count; ++i)
        workers.emplace_back (evaluate_rows);
      evaluate_rows ();
    }

    return ScalarRaster (domain, std::move (output));
  }

  ScalarRaster normalize (const ScalarRaster& raster) {
    const float minimum = raster.min_value ();
    const float range = raster.max_value () - minimum;
    const float scale = range != 0.0f ? 1.0f / range : 0.0f;
    std::vector<float> values;
    values.reserve (raster.values ().size ());
    for (float value : raster.values ())
      values.push_back ((value - minimum) * scale);
    return ScalarRaster (raster.domain (), std::move (values));
  }
}
